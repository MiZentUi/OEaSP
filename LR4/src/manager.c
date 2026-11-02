#include "manager.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "model.h"
#include "shared.h"
#include "timing.h"
#include "worker.h"

#ifndef STUDENT_ID
#define STUDENT_ID 103
#endif

static void make_shared_name(char *buffer, size_t size)
{
    snprintf(buffer, size, "/ossp_lr4_%d_%ld_%llu", STUDENT_ID,
             (long)getpid(),
             (unsigned long long)(timing_now_ns() & 0xffffffffULL));
}

static void child_main(const char *name, const cli_options_t *options,
                       shared_state_t *parent_state, worker_role_t role,
                       int index, int slot)
{
    shared_state_t *state = NULL;
    int rc;

    cli_bind(NULL, false);
    // the child opens its own mapping instead of using the inherited address
    shared_detach(parent_state);

    if (shared_attach(name, &state) < 0) {
        cli_error("ERROR worker attach failed: %s", strerror(errno));
        _exit(1);
    }
    if (state->magic != SHARED_MAGIC) {
        cli_error("ERROR worker shared magic mismatch");
        shared_detach(state);
        _exit(1);
    }

    cli_bind(state, options->quiet);
    rc = worker_run(state, options, role, index, slot);
    cli_bind(NULL, false);
    shared_detach(state);
    _exit(rc == 0 ? 0 : 1);
}

static void terminate_children(pid_t *pids, int count)
{
    int index;
    int status;

    for (index = 0; index < count; ++index) {
        (void)kill(pids[index], SIGTERM);
    }

    for (index = 0; index < count; ++index) {
        while (waitpid(pids[index], &status, 0) < 0 && errno == EINTR) {
        }
    }
}

static bool table_is_consistent(shared_state_t *state)
{
    bool consistent = true;

    if (state->magic != SHARED_MAGIC) {
        cli_error("ERROR invariant shared magic is invalid");
        consistent = false;
    }

    if (model_check(state) != 0) {
        cli_error("ERROR invariant table is inconsistent");
        consistent = false;
    }

    return consistent;
}

int manager_run(const cli_options_t *options)
{
    char name[64] = "";
    shared_state_t *state = NULL;
    pid_t pids[MAX_PARTICIPANTS];
    int participants = options->writers + options->readers;
    int started = 0;
    int child_failures = 0;
    bool initialized = false;
    bool launch_failed = false;
    bool table_consistent = false;
    int result = 1;
    int index;

    memset(pids, 0, sizeof(pids));

    if (participants < 1 || participants > MAX_PARTICIPANTS) {
        cli_error("ERROR invalid participant count: %d", participants);
        return 1;
    }

    make_shared_name(name, sizeof(name));
    if (shared_create(name, &state) < 0) {
        cli_error("ERROR shm_open failed: %s", strerror(errno));
        return 1;
    }

    if (shared_state_init(state) < 0) {
        cli_error("ERROR shared state init failed: %s", strerror(errno));
        goto cleanup;
    }
    initialized = true;
    model_init(state);

    cli_bind(state, options->quiet);

    for (index = 0; index < participants; ++index) {
        worker_role_t role;
        int worker_index;
        pid_t pid;

        if (index < options->writers) {
            role = ROLE_WRITER;
            worker_index = index;
        } else {
            role = ROLE_READER;
            worker_index = index - options->writers;
        }

        pid = fork();
        if (pid < 0) {
            cli_error("ERROR fork failed: %s", strerror(errno));
            launch_failed = true;
            break;
        }
        if (pid == 0) {
            child_main(name, options, state, role, worker_index, index);
        }

        pids[index] = pid;
        started++;
    }

    if (!launch_failed) {
        if (clock_gettime(CLOCK_MONOTONIC, &state->start_time) < 0) {
            cli_error("ERROR clock_gettime failed: %s", strerror(errno));
            launch_failed = true;
        } else {
            cli_event("MANAGER pid=%d START writers=%d readers=%d records=%d "
                      "ops=%lu write_us=%ld read_us=%ld",
                      (int)getpid(), options->writers, options->readers,
                      TABLE_SIZE, options->operations, options->write_us,
                      options->read_us);
        }
    }

    if (!launch_failed) {
        for (index = 0; index < participants; ++index) {
            if (sem_post(&state->start_gate) < 0) {
                cli_error("ERROR start_gate post failed: %s",
                          strerror(errno));
                launch_failed = true;
                break;
            }
        }
    }

    if (launch_failed) {
        terminate_children(pids, started);
        child_failures += participants - started;
    } else {
        for (index = 0; index < participants; ++index) {
            int status;
            pid_t pid;

            do {
                pid = waitpid(pids[index], &status, 0);
            } while (pid < 0 && errno == EINTR);

            if (pid < 0) {
                cli_error("ERROR waitpid failed: %s", strerror(errno));
                child_failures++;
                continue;
            }

            if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
                child_failures++;
                if (WIFEXITED(status)) {
                    cli_error("ERROR child pid=%ld exit=%d", (long)pid,
                              WEXITSTATUS(status));
                } else if (WIFSIGNALED(status)) {
                    cli_error("ERROR child pid=%ld signal=%d", (long)pid,
                              WTERMSIG(status));
                }
            }
        }
    }

    table_consistent = table_is_consistent(state);
    cli_report(options, state, table_consistent, child_failures);
    result = table_consistent && child_failures == 0 && !launch_failed ? 0 : 1;

cleanup:
    cli_bind(NULL, false);
    if (state != NULL) {
        if (initialized) {
            shared_state_destroy(state);
        }
        shared_detach(state);
    }
    if (name[0] != '\0' && shared_remove(name) < 0) {
        cli_error("ERROR shm_unlink failed: %s", strerror(errno));
    }
    return result;
}
