#include "worker.h"

#include <errno.h>
#include <inttypes.h>
#include <stdatomic.h>
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

static void note_invariant(shared_state_t *state, worker_role_t role,
                           int index, const char *description)
{
    atomic_fetch_add(&state->invariant_errors, 1);
    cli_error("%s %d pid=%d INVARIANT %s", role_name(role), index,
              (int)getpid(), description);
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

    cli_event("%s %d pid=%d START records=%d", name, index,
              (int)getpid(), TABLE_SIZE);

    for (operation = 1; operation <= options->operations; ++operation) {
        uint64_t wait_ns = 0;
        long duration_us;
        int rc;

        cli_event("%s %d pid=%d WAITING ACCESS=%s op=%lu", name, index,
                  (int)getpid(), access, operation);

        if (role == ROLE_WRITER) {
            rc = rwlock_writer_enter(state, options->timeout_ms, &wait_ns);
        } else {
            rc = rwlock_reader_enter(state, options->timeout_ms, &wait_ns);
        }

        stats->wait_ns += wait_ns;
        if (wait_ns > stats->max_wait_ns) {
            stats->max_wait_ns = wait_ns;
        }

        if (rc == ETIMEDOUT) {
            stats->timeouts++;
            cli_event("%s %d pid=%d TIMEOUT ACCESS=%s op=%lu wait_ms=%.3f",
                      name, index, (int)getpid(), access, operation,
                      (double)wait_ns / 1000000.0);
            continue;
        }
        if (rc != 0) {
            cli_error("ERROR %s %d pid=%d access=%s op=%lu error=%s", name,
                      index, (int)getpid(), access, operation, strerror(rc));
            return 1;
        }

        if (role == ROLE_WRITER) {
            cli_event("%s %d pid=%d ACQUIRED ACCESS=WRITE op=%lu", name,
                      index, (int)getpid(), operation);
            model_write(state);
            if (atomic_load(&state->active_readers) != 0) {
                note_invariant(state, role, index, "reader active during write");
            }
            stats->records += TABLE_SIZE;
            cli_event("%s %d pid=%d WRITING generation=%u records=%d", name,
                      index, (int)getpid(), state->generation, TABLE_SIZE);
        } else {
            cli_event("%s %d pid=%d ACQUIRED ACCESS=READ op=%lu readers=%d",
                      name, index, (int)getpid(), operation,
                      atomic_load(&state->active_readers));
            if (model_check(state) != 0) {
                note_invariant(state, role, index, "inconsistent table");
            }
            if (atomic_load(&state->active_writers) != 0) {
                note_invariant(state, role, index, "writer active during read");
            }
            stats->records += TABLE_SIZE;
            cli_event("%s %d pid=%d READING generation=%u records=%d", name,
                      index, (int)getpid(), state->generation, TABLE_SIZE);
        }

        if (role == ROLE_WRITER) {
            duration_us = operation_duration(options->write_us,
                                             &random_state);
        } else {
            duration_us = operation_duration(options->read_us, &random_state);
        }
        // this delay models the operation time, synchronization is separate
        timing_sleep_us(duration_us);

        if (role == ROLE_WRITER) {
            rc = rwlock_writer_leave(state);
        } else {
            rc = rwlock_reader_leave(state);
        }
        if (rc != 0) {
            cli_error("ERROR %s %d pid=%d release=%s op=%lu error=%s", name,
                      index, (int)getpid(), access, operation, strerror(rc));
            return 1;
        }

        stats->operations++;
        cli_event("%s %d pid=%d RELEASED ACCESS=%s op=%lu wait_ms=%.3f",
                  name, index, (int)getpid(), access, operation,
                  (double)wait_ns / 1000000.0);
    }

    cli_event("%s %d pid=%d FINISHED ops=%" PRIu64 " timeouts=%" PRIu64,
              name, index, (int)getpid(), stats->operations,
              stats->timeouts);
    return 0;
}
