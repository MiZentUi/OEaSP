#ifndef CLI_H
#define CLI_H

#include <stdbool.h>
#include <stdint.h>

#include "shared.h"

enum {
    CLI_OK = 0,
    CLI_HELP = 1,
    CLI_ERROR = 2
};

typedef struct {
    unsigned long operations;
    long write_us;
    long read_us;
    unsigned int seed;
} cli_options_t;

int cli_parse(int argc, char **argv, cli_options_t *options);
void cli_usage(void);

void cli_error(const char *format, ...);
void cli_report(const cli_options_t *options, const shared_state_t *state,
                bool table_consistent, int child_failures);

#endif
