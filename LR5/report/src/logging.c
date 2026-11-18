#include "logging.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "timing.h"

static bool g_quiet;

void log_set_quiet(bool quiet)
{
    g_quiet = quiet;
}

bool log_is_quiet(void)
{
    return g_quiet;
}

static void write_log(int fd, const char *role, const char *format, va_list args)
{
    char timestamp[32];
    char message[1024];
    char line[1200];
    int length;

    timing_format_now(timestamp, sizeof(timestamp));
    vsnprintf(message, sizeof(message), format, args);
    length = snprintf(line, sizeof(line), "[%s] ROLE=%s %s\n",
                      timestamp, role, message);
    if (length < 0) {
        return;
    }
    if ((size_t)length >= sizeof(line)) {
        length = (int)sizeof(line) - 1;
        line[length - 1] = '\n';
    }
    (void)write(fd, line, (size_t)length);
}

void log_event(const char *role, const char *format, ...)
{
    va_list args;

    if (g_quiet) {
        return;
    }
    va_start(args, format);
    write_log(STDOUT_FILENO, role, format, args);
    va_end(args);
}

void log_error(const char *role, const char *format, ...)
{
    va_list args;

    va_start(args, format);
    write_log(STDERR_FILENO, role, format, args);
    va_end(args);
}
