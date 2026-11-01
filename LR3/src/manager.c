#include "manager.h"

#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "bench_client.h"
#include "client.h"
#include "control.h"
#include "logging.h"
#include "process.h"
#include "protocol.h"
#include "server.h"
#include "transport.h"

// starts three clients and waits for them to finish
static int run_clients(void)
{
    pid_t clients[CLIENT_COUNT];
    int started = 0;
    int failures = 0;

    for (int index = 0; index < CLIENT_COUNT; ++index) {
        pid_t client_pid = fork();

        if (client_pid < 0) {
            journal_error("ERROR func=fork count=%d", index + 1);
            ++failures;
            break;
        }
        if (client_pid == 0) {
            set_role("CLIENT#%d", index + 1);
            _exit(client_main((uint32_t)index + 1));
        }
        clients[started++] = client_pid;
        journal("FORK role=CLIENT#%d pid=%ld",
                index + 1, (long)client_pid);
    }

    for (int index = 0; index < started; ++index) {
        char role[16];

        snprintf(role, sizeof(role), "CLIENT#%d", index + 1);
        failures += wait_for_child(clients[index], role);
    }

    return failures;
}

int manager_main(bool bench_mode)
{
    char path[108];
    pid_t server_pid;
    int failures = 0;

    make_socket_path(path, sizeof(path));

    if (unlink(path) == 0) {
        journal("CLEANUP removed stale socket %s", path);
    } else if (errno != ENOENT) {
        fatal_errno("unlink");
    }

    journal("START mode=%s clients=%d messages_per_client=%d student_id=%d",
            bench_mode ? "bench" : "run",
            bench_mode ? 1 : CLIENT_COUNT, CLIENT_MESSAGES, STUDENT_ID);

    server_pid = fork();
    if (server_pid < 0) {
        fatal_errno("fork");
    }
    if (server_pid == 0) {
        set_role("SERVER");
        _exit(server_main());
    }

    journal("FORK role=SERVER pid=%ld", (long)server_pid);

    if (wait_for_socket_path(server_pid, path) < 0) {
        journal_error("ERROR server did not create socket %s", path);
        if (kill(server_pid, 0) == 0) {
            kill(server_pid, SIGTERM);
        }
        wait_for_child(server_pid, "SERVER");
        unlink(path);
        return EXIT_FAILURE;
    }
    journal("SERVER READY pid=%ld path=%s", (long)server_pid, path);

    if (bench_mode) {
        pid_t client_pid = fork();

        if (client_pid < 0) {
            fatal_errno("fork");
        }
        if (client_pid == 0) {
            set_role("BENCH");
            _exit(bench_client_main());
        }
        journal("FORK role=BENCH pid=%ld", (long)client_pid);
        failures += wait_for_child(client_pid, "BENCH");
    } else {
        failures += run_clients();
    }

    if (send_stop() < 0) {
        ++failures;
    }

    failures += wait_for_child(server_pid, "SERVER");

    if (unlink(path) < 0 && errno != ENOENT) {
        journal_error("ERROR func=unlink errno=%d (%s)",
                      errno, strerror(errno));
        ++failures;
    }

    journal("SUMMARY failures=%d result=%s",
            failures, failures == 0 ? "OK" : "FAIL");
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
