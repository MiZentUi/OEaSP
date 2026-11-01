#include "logging.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "protocol.h"

static uint32_t g_run_id;
static char g_role[32] = "UNKNOWN";

void logging_set_run_id(uint32_t run_id)
{
    g_run_id = run_id;
}

uint32_t logging_run_id(void)
{
    return g_run_id;
}

void set_role(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(g_role, sizeof(g_role), fmt, ap);
    va_end(ap);
}

// shared journal line output implementation using va_list
static void vjournal(FILE *stream, const char *fmt, va_list ap)
{
    struct timespec now;
    struct tm tm;
    char clock_text[16];

    clock_gettime(CLOCK_REALTIME, &now);
    localtime_r(&now.tv_sec, &tm);
    strftime(clock_text, sizeof(clock_text), "%H:%M:%S", &tm);

    fprintf(stream, "[%s.%03ld] PID=%ld VARIANT=%d RUN_ID=%u ROLE=%s ",
            clock_text, now.tv_nsec / 1000000L, (long)getpid(),
            VARIANT_ID, g_run_id, g_role);
    vfprintf(stream, fmt, ap);
    fputc('\n', stream);
    fflush(stream);
}

void journal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vjournal(stdout, fmt, ap);
    va_end(ap);
}

void journal_error(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vjournal(stderr, fmt, ap);
    va_end(ap);
}

// prints errno and exits the process
void fatal_errno(const char *func)
{
    int error = errno;

    journal_error("ERROR func=%s errno=%d (%s)", func, error, strerror(error));
    exit(EXIT_FAILURE);
}

// formats message time as a journal string
void format_timestamp(int64_t seconds, int64_t nanoseconds,
                      char *out, size_t size)
{
    time_t value = (time_t)seconds;
    struct tm tm;
    char clock_text[16];

    if (localtime_r(&value, &tm) == NULL) {
        snprintf(out, size, "unknown");
        return;
    }
    strftime(clock_text, sizeof(clock_text), "%H:%M:%S", &tm);
    snprintf(out, size, "%s.%03ld", clock_text,
             (long)(nanoseconds / 1000000));
}

uint64_t timespec_diff_ns(const struct timespec *start,
                          const struct timespec *end)
{
    int64_t seconds = (int64_t)end->tv_sec - (int64_t)start->tv_sec;
    int64_t nanoseconds = (int64_t)end->tv_nsec - (int64_t)start->tv_nsec;

    return (uint64_t)(seconds * 1000000000LL + nanoseconds);
}
