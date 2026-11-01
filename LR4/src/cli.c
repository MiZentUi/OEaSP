#include "cli.h"

#include <stdarg.h>
#include <stdio.h>

void cli_error(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    fflush(stderr);
}

void cli_report(const shared_state_t *state, bool table_consistent)
{
    printf("\n");
    printf("=== LR4 shared table prototype ===\n");
    printf("records=%u generation=%u\n", state->table_size,
           state->generation);
    printf("table=%s\n", table_consistent ? "consistent" : "inconsistent");
    printf("RESULT: %s\n", table_consistent ? "OK" : "FAILED");
    fflush(stdout);
}
