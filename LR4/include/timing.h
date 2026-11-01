#ifndef TIMING_H
#define TIMING_H

#include <stdint.h>
#include <time.h>

uint64_t timing_now_ns(void);
uint64_t timing_diff_ns(const struct timespec *start,
                        const struct timespec *end);
void timing_sleep_us(long microseconds);
void timing_deadline_ms(int milliseconds, struct timespec *deadline);

#endif
