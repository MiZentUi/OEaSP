#include "manager.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>
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

#define ABORT_GRACE_MS 500
#define SUPERVISOR_STEP_US 5000

static volatile sig_atomic_t g_stop_requested;

static void handle_signal(int signal_number)
{
    (void)signal_number;
    g_stop_requested = 1;
}

static int install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = handle_signal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0) {
        return -1;
    }
    if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
        return -1;
    }
    return 0;
}

static void reset_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = SIG_DFL;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    (void)sigaction(SIGINT, &action, NULL);
    (void)sigaction(SIGTERM, &action, NULL);
}

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

    reset_signal_handlers();
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

static void terminate_children(pid_t *pids, bool *done, int count)
{
    int index;
    int status;

    for (index = 0; index < count; ++index) {
        if (!done[index]) {
            (void)kill(pids[index], SIGTERM);
        }
    }

    for (index = 0; index < count; ++index) {
        if (!done[index]) {
            while (waitpid(pids[index], &status, 0) < 0 && errno == EINTR) {
            }
            done[index] = true;
        }
    }
}

static void verify_final(shared_state_t *state)
{
    int gate_value = 0;
    unsigned int errors = 0;

    if (state->magic != SHARED_MAGIC) {
        cli_error("ERROR invariant shared magic is invalid");
        errors++;
    }

    if (model_check(state) != 0) {
        cli_error("ERROR invariant table is inconsistent");
        errors++;
    }

    if (atomic_load(&state->active_readers) != 0) {
        cli_error("ERROR invariant active_readers=%d",
                  atomic_load(&state->active_readers));
        errors++;
    }

    if (atomic_load(&state->active_writers) != 0) {
        cli_error("ERROR invariant active_writers=%d",
                  atomic_load(&state->active_writers));
        errors++;
    }

    if (state->readers_count != 0) {
        cli_error("ERROR invariant readers_count=%d", state->readers_count);
        errors++;
    }

    if (sem_getvalue(&state->access_gate, &gate_value) < 0) {
        cli_error("ERROR invariant access_gate status failed: %s",
                  strerror(errno));
        errors++;
    } else if (gate_value != 1) {
        cli_error("ERROR invariant access_gate value=%d", gate_value);
        errors++;
    }

    if (atomic_load(&state->peak_writers) > 1) {
        cli_error("ERROR invariant peak_writers=%d",
                  atomic_load(&state->peak_writers));
        errors++;
    }

    if (errors > 0) {
        atomic_fetch_add(&state->invariant_errors, errors);
    }
}

int manager_run(const cli_options_t *options)
{
    char name[64] = "";
    shared_state_t *state = NULL;
    pid_t pids[MAX_PARTICIPANTS];
    bool done[MAX_PARTICIPANTS];
    int participants = options->writers + options->readers;
    int started = 0;
    int remaining = 0;
    int child_failures = 0;
    bool initialized = false;
    bool aborted = false;
    bool term_sent = false;
    bool kill_sent = false;
    uint64_t experiment_start_ns = 0;
    uint64_t abort_start_ns = 0;
    int result = 1;
    int index;

    memset(pids, 0, sizeof(pids));
    memset(done, 0, sizeof(done));
    g_stop_requested = 0;

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

    if (install_signal_handlers() < 0) {
        cli_error("ERROR sigaction failed: %s", strerror(errno));
        goto cleanup;
    }

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
            aborted = true;
            break;
        }
        if (pid == 0) {
            child_main(name, options, state, role, worker_index, index);
        }

        pids[index] = pid;
        started++;
    }

    if (!aborted && started == participants) {
        if (clock_gettime(CLOCK_MONOTONIC, &state->start_time) < 0) {
            cli_error("ERROR clock_gettime failed: %s", strerror(errno));
            aborted = true;
        } else {
            experiment_start_ns = timing_now_ns();
            cli_event("MANAGER pid=%d START writers=%d readers=%d records=%d "
                      "ops=%lu write_us=%ld read_us=%ld",
                      (int)getpid(), options->writers, options->readers,
                      TABLE_SIZE, options->operations, options->write_us,
                      options->read_us);
        }
    }

    if (!aborted) {
        for (index = 0; index < participants; ++index) {
            if (sem_post(&state->start_gate) < 0) {
                cli_error("ERROR start_gate post failed: %s",
                          strerror(errno));
                aborted = true;
                break;
            }
        }
    }

    if (!aborted) {
        remaining = participants;

        while (remaining > 0) {
            uint64_t now;
            uint64_t elapsed_ms;
            int status;
            pid_t pid;

            now = timing_now_ns();
            elapsed_ms = experiment_start_ns == 0
                             ? 0
                             : (now - experiment_start_ns) / 1000000ULL;

            if (!aborted &&
                (g_stop_requested ||
                 elapsed_ms > (uint64_t)options->max_duration_ms)) {
                aborted = true;
                abort_start_ns = now;
                cli_event("MANAGER pid=%d ABORT cause=%s elapsed_ms=%llu",
                          (int)getpid(),
                          g_stop_requested ? "signal" : "timeout",
                          (unsigned long long)elapsed_ms);
            }

            if (aborted && !term_sent) {
                for (index = 0; index < participants; ++index) {
                    if (!done[index]) {
                        (void)kill(pids[index], SIGTERM);
                    }
                }
                term_sent = true;
            }

            if (aborted && term_sent && !kill_sent &&
                now - abort_start_ns > (uint64_t)ABORT_GRACE_MS * 1000000ULL) {
                for (index = 0; index < participants; ++index) {
                    if (!done[index]) {
                        (void)kill(pids[index], SIGKILL);
                    }
                }
                kill_sent = true;
            }

            pid = waitpid(-1, &status, WNOHANG);
            if (pid > 0) {
                for (index = 0; index < participants; ++index) {
                    if (pids[index] == pid && !done[index]) {
                        break;
                    }
                }

                if (index == participants) {
                    cli_error("ERROR unknown child pid=%ld", (long)pid);
                    aborted = true;
                    continue;
                }

                done[index] = true;
                remaining--;

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
                continue;
            }

            if (pid < 0) {
                if (errno == EINTR) {
                    continue;
                }
                if (errno == ECHILD) {
                    aborted = true;
                    break;
                }
                cli_error("ERROR waitpid failed: %s", strerror(errno));
                aborted = true;
                break;
            }

            timing_sleep_us(SUPERVISOR_STEP_US);
        }
    } else {
        terminate_children(pids, done, started);
    }

    verify_final(state);
    cli_report(options, state, aborted, child_failures);
    result = !aborted && child_failures == 0 &&
                     atomic_load(&state->invariant_errors) == 0
                 ? 0
                 : 1;

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
