#ifndef LR5_LOGGING_H
#define LR5_LOGGING_H

#include <stdbool.h>

void log_set_quiet(bool quiet);
bool log_is_quiet(void);
void log_event(const char *role, const char *format, ...)
    __attribute__((format(printf, 2, 3)));
void log_error(const char *role, const char *format, ...)
    __attribute__((format(printf, 2, 3)));

#endif
