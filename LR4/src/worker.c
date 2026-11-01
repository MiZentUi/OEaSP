#include "worker.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "model.h"
#include "sync.h"
#include "timing.h"

static const char *role_name(worker_role_t role)
{
    return role == ROLE_WRITER ? "WRITER" : "READER";
}

static const char *access_name(worker_role_t role)
{
    return role == ROLE_WRITER ? "WRITE" : "READ";
}

static long operation_duration(long base_us, uint32_t *random_state)
{
    long delta;
    unsigned long span;
    unsigned long offset;

    if (base_us <= 0) {
        return 0;
    }

    delta = base_us / 10;
    if (delta == 0) {
        return base_us;
    }

    span = (unsigned long)(2 * delta + 1);
    offset = (unsigned long)rand_r(random_state) % span;
    return base_us - delta + (long)offset;
}

int worker_run(shared_state_t *state, const cli_options_t *options,
               worker_role_t role, int index, int slot)
{
    worker_stats_t *stats = &state->stats[slot];
    uint32_t random_state;
    unsigned long operation;
    const char *name = role_name(role);
    const char *access = access_name(role);

    stats->role = role;
    stats->index = index;
    stats->pid = (int)getpid();
    random_state = options->seed ^ ((uint32_t)(slot + 1) * 2654435761u);

    while (sem_wait(&state->start_gate) < 0) {
        if (errno == EINTR) {
            continue;
        }
        cli_error("ERROR %s %d pid=%d start_gate=%s", name, index,
                  (int)getpid(), strerror(errno));
        return 1;
    }

    for (operation = 1; operation <= options->operations; ++operation) {
        long duration_us;
        int rc;

        rc = table_enter(state);
        if (rc != 0) {
            cli_error("ERROR %s %d pid=%d access=%s op=%lu error=%s", name,
                      index, (int)getpid(), access, operation, strerror(rc));
            return 1;
        }

        if (role == ROLE_WRITER) {
            model_write(state);
            stats->records += TABLE_SIZE;
        } else {
            if (model_check(state) != 0) {
                cli_error("ERROR %s %d pid=%d inconsistent table", name,
                          index, (int)getpid());
                table_leave(state);
                return 1;
            }
            stats->records += TABLE_SIZE;
        }

        if (role == ROLE_WRITER) {
            duration_us = operation_duration(options->write_us,
                                             &random_state);
        } else {
            duration_us = operation_duration(options->read_us, &random_state);
        }
        // this delay models the operation time, synchronization is separate
        timing_sleep_us(duration_us);

        rc = table_leave(state);
        if (rc != 0) {
            cli_error("ERROR %s %d pid=%d release=%s op=%lu error=%s", name,
                      index, (int)getpid(), access, operation, strerror(rc));
            return 1;
        }

        stats->operations++;
    }

    return 0;
}
