#include "process.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "logging.h"

static void sleep_ms(long milliseconds)
{
    struct timespec delay;

    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (milliseconds % 1000) * 1000000L;
    nanosleep(&delay, NULL);
}

// waits for the socket file to appear after the server starts
int wait_for_socket_path(pid_t pid, const char *path)
{
    // the server binds the socket itself so clients start after bind
    for (int attempt = 0; attempt < 250; ++attempt) {
        struct stat info;

        if (stat(path, &info) == 0 && S_ISSOCK(info.st_mode)) {
            return 0;
        }
        if (kill(pid, 0) < 0 && errno == ESRCH) {
            return -1;
        }
        sleep_ms(20);
    }
    return -1;
}

int wait_for_child(pid_t pid, const char *role)
{
    int status;

    if (waitpid(pid, &status, 0) < 0) {
        int error = errno;

        journal_error("ERROR func=waitpid role=%s pid=%ld errno=%d (%s)",
                      role, (long)pid, error, strerror(error));
        return 1;
    }

    if (WIFEXITED(status)) {
        int code = WEXITSTATUS(status);

        if (code == 0) {
            journal("CHILD DONE role=%s pid=%ld", role, (long)pid);
            return 0;
        }
        journal_error("ERROR child role=%s pid=%ld exit_code=%d",
                      role, (long)pid, code);
        return 1;
    }
    if (WIFSIGNALED(status)) {
        journal_error("ERROR child role=%s pid=%ld signal=%d",
                      role, (long)pid, WTERMSIG(status));
        return 1;
    }

    journal_error("ERROR child role=%s pid=%ld status=0x%x",
                  role, (long)pid, status);
    return 1;
}
