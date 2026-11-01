#ifndef SHARED_H
#define SHARED_H

#include <stdint.h>

#define TABLE_SIZE 100
#define SHARED_MAGIC 0x4c523400u

typedef struct {
    uint32_t id;
    uint32_t generation;
    uint32_t value;
} record_t;

typedef struct shared_state {
    uint32_t magic;
    uint32_t table_size;
    uint32_t generation;
    record_t table[TABLE_SIZE];
} shared_state_t;

int shared_create(const char *name, shared_state_t **out);
int shared_attach(const char *name, shared_state_t **out);
int shared_detach(shared_state_t *state);
int shared_remove(const char *name);
int shared_state_init(shared_state_t *state);
void shared_state_destroy(shared_state_t *state);

#endif
