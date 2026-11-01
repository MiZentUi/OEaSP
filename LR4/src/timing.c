#include "timing.h"

#include <errno.h>

uint64_t timing_now_ns(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * 1000000000ULL + (uint64_t)now.tv_nsec;
}

uint64_t timing_diff_ns(const struct timespec *start,
                        const struct timespec *end)
{
    int64_t seconds = (int64_t)end->tv_sec - (int64_t)start->tv_sec;
    int64_t nanoseconds = (int64_t)end->tv_nsec - (int64_t)start->tv_nsec;

    return (uint64_t)(seconds * 1000000000LL + nanoseconds);
}

void timing_sleep_us(long microseconds)
{
    struct timespec request;
    struct timespec remaining;

    if (microseconds <= 0) {
        return;
    }

    request.tv_sec = microseconds / 1000000L;
    request.tv_nsec = (microseconds % 1000000L) * 1000L;

    while (clock_nanosleep(CLOCK_MONOTONIC, 0, &request, &remaining) == EINTR) {
        request = remaining;
    }
}

void timing_deadline_ms(int milliseconds, struct timespec *deadline)
{
    long nanoseconds;

    clock_gettime(CLOCK_REALTIME, deadline);
    deadline->tv_sec += milliseconds / 1000;
    nanoseconds = deadline->tv_nsec + (milliseconds % 1000) * 1000000L;
    deadline->tv_sec += nanoseconds / 1000000000L;
    deadline->tv_nsec = nanoseconds % 1000000000L;
}
