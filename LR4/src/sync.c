#include "sync.h"

#include <errno.h>

#include "timing.h"

static int wait_semaphore(sem_t *semaphore, const struct timespec *deadline)
{
    while (sem_timedwait(semaphore, deadline) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return errno;
    }
    return 0;
}

static int recover_owner(pthread_mutex_t *mutex, shared_state_t *state, int rc)
{
    int recover;

    if (rc != EOWNERDEAD) {
        return rc;
    }

    atomic_fetch_add(&state->invariant_errors, 1);
    recover = pthread_mutex_consistent(mutex);
    if (recover != 0) {
        pthread_mutex_unlock(mutex);
        return recover;
    }
    return 0;
}

static int lock_mutex_timed(pthread_mutex_t *mutex, shared_state_t *state,
                            const struct timespec *deadline)
{
    int rc;

    do {
        rc = pthread_mutex_timedlock(mutex, deadline);
    } while (rc == EINTR);

    return recover_owner(mutex, state, rc);
}

static int lock_mutex(pthread_mutex_t *mutex, shared_state_t *state)
{
    int rc;

    do {
        rc = pthread_mutex_lock(mutex);
    } while (rc == EINTR);

    return recover_owner(mutex, state, rc);
}

static void note_violation(shared_state_t *state)
{
    atomic_fetch_add(&state->invariant_errors, 1);
}

static void update_reader_peak(shared_state_t *state)
{
    int current = atomic_load(&state->active_readers);
    int peak = atomic_load(&state->peak_readers);

    while (current > peak &&
           !atomic_compare_exchange_weak(&state->peak_readers, &peak, current)) {
    }
}

static void update_writer_peak(shared_state_t *state)
{
    int current = atomic_load(&state->active_writers);
    int peak = atomic_load(&state->peak_writers);

    while (current > peak &&
           !atomic_compare_exchange_weak(&state->peak_writers, &peak, current)) {
    }
}

int rwlock_reader_enter(shared_state_t *state, int timeout_ms,
                        uint64_t *wait_ns)
{
    struct timespec deadline;
    uint64_t start = timing_now_ns();
    int rc;

    timing_deadline_ms(timeout_ms, &deadline);

    rc = wait_semaphore(&state->turnstile, &deadline);
    if (rc != 0) {
        goto done;
    }

    rc = lock_mutex_timed(&state->readers_mutex, state, &deadline);
    if (rc != 0) {
        sem_post(&state->turnstile);
        goto done;
    }

    state->readers_count++;
    if (state->readers_count == 1) {
        // the first reader holds the gate for the whole read group
        rc = wait_semaphore(&state->access_gate, &deadline);
        if (rc != 0) {
            state->readers_count--;
            pthread_mutex_unlock(&state->readers_mutex);
            sem_post(&state->turnstile);
            goto done;
        }
    }

    atomic_fetch_add(&state->active_readers, 1);
    update_reader_peak(state);

    if (atomic_load(&state->active_writers) != 0) {
        note_violation(state);
    }

    pthread_mutex_unlock(&state->readers_mutex);
    sem_post(&state->turnstile);
    rc = 0;

done:
    if (wait_ns != NULL) {
        *wait_ns = timing_now_ns() - start;
    }
    return rc;
}

int rwlock_reader_leave(shared_state_t *state)
{
    int rc;
    int previous;

    rc = lock_mutex(&state->readers_mutex, state);
    if (rc != 0) {
        return rc;
    }

    state->readers_count--;
    if (state->readers_count < 0) {
        note_violation(state);
        state->readers_count = 0;
    }

    previous = atomic_fetch_sub(&state->active_readers, 1);
    if (previous <= 0) {
        note_violation(state);
    }

    if (state->readers_count == 0 && sem_post(&state->access_gate) < 0) {
        rc = errno;
    }

    pthread_mutex_unlock(&state->readers_mutex);
    return rc;
}

int rwlock_writer_enter(shared_state_t *state, int timeout_ms,
                        uint64_t *wait_ns)
{
    struct timespec deadline;
    uint64_t start = timing_now_ns();
    int rc;
    int previous;

    timing_deadline_ms(timeout_ms, &deadline);

    rc = wait_semaphore(&state->turnstile, &deadline);
    if (rc != 0) {
        goto done;
    }

    // a waiting writer closes the turnstile for new readers
    rc = wait_semaphore(&state->access_gate, &deadline);
    if (rc != 0) {
        sem_post(&state->turnstile);
        goto done;
    }

    sem_post(&state->turnstile);

    if (atomic_load(&state->active_readers) != 0) {
        note_violation(state);
    }

    // only one writer can own the access gate
    previous = atomic_fetch_add(&state->active_writers, 1);
    if (previous != 0) {
        note_violation(state);
    }
    update_writer_peak(state);

done:
    if (wait_ns != NULL) {
        *wait_ns = timing_now_ns() - start;
    }
    return rc;
}

int rwlock_writer_leave(shared_state_t *state)
{
    int previous = atomic_fetch_sub(&state->active_writers, 1);
    int rc = 0;

    if (previous != 1) {
        note_violation(state);
    }

    if (sem_post(&state->access_gate) < 0) {
        rc = errno;
    }
    return rc;
}
