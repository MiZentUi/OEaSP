#ifndef SYNC_H
#define SYNC_H

#include <stdint.h>

#include "shared.h"

int rwlock_reader_enter(shared_state_t *state, int timeout_ms,
                        uint64_t *wait_ns);
int rwlock_reader_leave(shared_state_t *state);
int rwlock_writer_enter(shared_state_t *state, int timeout_ms,
                        uint64_t *wait_ns);
int rwlock_writer_leave(shared_state_t *state);

#endif
