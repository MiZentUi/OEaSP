#ifndef WORKER_H
#define WORKER_H

#include "cli.h"
#include "shared.h"

int worker_run(shared_state_t *state, const cli_options_t *options,
               worker_role_t role, int index, int slot);

#endif
