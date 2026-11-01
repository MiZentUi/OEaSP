#ifndef CLI_H
#define CLI_H

#include <stdbool.h>

#include "shared.h"

void cli_error(const char *format, ...);
void cli_report(const shared_state_t *state, bool table_consistent);

#endif
