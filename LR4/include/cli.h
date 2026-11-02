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
    int writers;
    int readers;
    unsigned long operations;
    long write_us;
    long read_us;
    int timeout_ms;
    unsigned int seed;
    bool quiet;
} cli_options_t;

int cli_parse(int argc, char **argv, cli_options_t *options);
void cli_usage(void);

void cli_bind(shared_state_t *state, bool quiet);
void cli_event(const char *format, ...);
void cli_error(const char *format, ...);
void cli_report(const cli_options_t *options, const shared_state_t *state,
                int child_failures);

#endif
