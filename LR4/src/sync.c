#include "sync.h"

#include <errno.h>

#include "timing.h"

static int wait_semaphore(sem_t *semaphore)
{
    while (sem_wait(semaphore) < 0) {
        if (errno == EINTR) {
            continue;
        }
        return errno;
    }
    return 0;
}

static int recover_owner(pthread_mutex_t *mutex, int rc)
{
    int recover;

    if (rc != EOWNERDEAD) {
        return rc;
    }

    recover = pthread_mutex_consistent(mutex);
    if (recover != 0) {
        pthread_mutex_unlock(mutex);
        return recover;
    }
    return 0;
}

static int lock_mutex(pthread_mutex_t *mutex)
{
    int rc;

    do {
        rc = pthread_mutex_lock(mutex);
    } while (rc == EINTR);

    return recover_owner(mutex, rc);
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

int rwlock_reader_enter(shared_state_t *state, uint64_t *wait_ns)
{
    uint64_t start = timing_now_ns();
    int rc;

    rc = wait_semaphore(&state->turnstile);
    if (rc != 0) {
        goto done;
    }

    rc = lock_mutex(&state->readers_mutex);
    if (rc != 0) {
        sem_post(&state->turnstile);
        goto done;
    }

    state->readers_count++;
    if (state->readers_count == 1) {
        // the first reader holds the gate for the whole read group
        rc = wait_semaphore(&state->access_gate);
        if (rc != 0) {
            state->readers_count--;
            pthread_mutex_unlock(&state->readers_mutex);
            sem_post(&state->turnstile);
            goto done;
        }
    }

    atomic_fetch_add(&state->active_readers, 1);
    update_reader_peak(state);

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

    rc = lock_mutex(&state->readers_mutex);
    if (rc != 0) {
        return rc;
    }

    state->readers_count--;
    atomic_fetch_sub(&state->active_readers, 1);

    if (state->readers_count == 0 && sem_post(&state->access_gate) < 0) {
        rc = errno;
    }

    pthread_mutex_unlock(&state->readers_mutex);
    return rc;
}

int rwlock_writer_enter(shared_state_t *state, uint64_t *wait_ns)
{
    uint64_t start = timing_now_ns();
    int rc;

    rc = wait_semaphore(&state->turnstile);
    if (rc != 0) {
        goto done;
    }

    // a waiting writer closes the turnstile for new readers
    rc = wait_semaphore(&state->access_gate);
    if (rc != 0) {
        sem_post(&state->turnstile);
        goto done;
    }

    sem_post(&state->turnstile);

    atomic_fetch_add(&state->active_writers, 1);
    update_writer_peak(state);
    rc = 0;

done:
    if (wait_ns != NULL) {
        *wait_ns = timing_now_ns() - start;
    }
    return rc;
}

int rwlock_writer_leave(shared_state_t *state)
{
    int rc = 0;

    atomic_fetch_sub(&state->active_writers, 1);

    if (sem_post(&state->access_gate) < 0) {
        rc = errno;
    }
    return rc;
}
