#include "timing.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>

uint64_t timing_now_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

void timing_deadline_from_ms(int milliseconds, struct timespec *deadline)
{
    clock_gettime(CLOCK_MONOTONIC, deadline);
    deadline->tv_sec += milliseconds / 1000;
    deadline->tv_nsec += (long)(milliseconds % 1000) * 1000000L;
    if (deadline->tv_nsec >= 1000000000L) {
        deadline->tv_sec += 1;
        deadline->tv_nsec -= 1000000000L;
    }
}

int timing_remaining_ms(const struct timespec *deadline)
{
    struct timespec now;
    long long nanoseconds;
    long long milliseconds;

    clock_gettime(CLOCK_MONOTONIC, &now);
    nanoseconds = (long long)(deadline->tv_sec - now.tv_sec) * 1000000000LL
                + (long long)(deadline->tv_nsec - now.tv_nsec);
    if (nanoseconds <= 0) {
        return 0;
    }
    milliseconds = nanoseconds / 1000000LL;
    if (milliseconds == 0) {
        milliseconds = 1;
    }
    if (milliseconds > 2147483647LL) {
        milliseconds = 2147483647LL;
    }
    return (int)milliseconds;
}

double timing_diff_ms(uint64_t start_ns, uint64_t end_ns)
{
    if (end_ns <= start_ns) {
        return 0.0;
    }
    return (double)(end_ns - start_ns) / 1000000.0;
}

void timing_format_now(char *buffer, size_t size)
{
    struct timeval now;
    struct tm local;
    int milliseconds;

    if (size == 0) {
        return;
    }
    if (gettimeofday(&now, NULL) != 0) {
        snprintf(buffer, size, "00:00:00.000");
        return;
    }
    milliseconds = (int)(now.tv_usec / 1000);
    if (localtime_r(&now.tv_sec, &local) == NULL) {
        snprintf(buffer, size, "00:00:00.%03d", milliseconds);
        return;
    }
    snprintf(buffer, size, "%02d:%02d:%02d.%03d",
             local.tm_hour, local.tm_min, local.tm_sec, milliseconds);
}
