#include "sync.h"

#include <errno.h>

static int lock_table(shared_state_t *state)
{
    int rc;

    do {
        rc = pthread_mutex_lock(&state->table_mutex);
    } while (rc == EINTR);

    if (rc == EOWNERDEAD) {
        rc = pthread_mutex_consistent(&state->table_mutex);
        if (rc != 0) {
            pthread_mutex_unlock(&state->table_mutex);
        }
    }
    return rc;
}

int table_enter(shared_state_t *state)
{
    // one mutex gives exclusive access to the whole table
    return lock_table(state);
}

int table_leave(shared_state_t *state)
{
    return pthread_mutex_unlock(&state->table_mutex);
}
