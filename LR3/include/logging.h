#ifndef LOGGING_H
#define LOGGING_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

void logging_set_run_id(uint32_t run_id);
uint32_t logging_run_id(void);
void set_role(const char *fmt, ...);
void journal(const char *fmt, ...);
void journal_error(const char *fmt, ...);
void fatal_errno(const char *func);
void format_timestamp(int64_t seconds, int64_t nanoseconds,
                      char *out, size_t size);
uint64_t timespec_diff_ns(const struct timespec *start,
                          const struct timespec *end);

#endif
