#ifndef LR5_TIMING_H
#define LR5_TIMING_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

uint64_t timing_now_ns(void);
void timing_deadline_from_ms(int milliseconds, struct timespec *deadline);
int timing_remaining_ms(const struct timespec *deadline);
double timing_diff_ms(uint64_t start_ns, uint64_t end_ns);
void timing_format_now(char *buffer, size_t size);

#endif
