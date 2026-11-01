#include "model.h"

#include <stddef.h>

void model_init(shared_state_t *state)
{
    size_t index;

    state->generation = 0;
    state->table_size = TABLE_SIZE;

    for (index = 0; index < TABLE_SIZE; ++index) {
        state->table[index].id = (uint32_t)index;
        state->table[index].generation = 0;
        state->table[index].value = (uint32_t)index;
    }
}

void model_write(shared_state_t *state)
{
    size_t index;
    uint32_t generation = state->generation + 1;

    // one write creates a new generation for all records
    for (index = 0; index < TABLE_SIZE; ++index) {
        state->table[index].id = (uint32_t)index;
        state->table[index].generation = generation;
        state->table[index].value = generation * TABLE_SIZE + (uint32_t)index;
    }
    state->generation = generation;
}

int model_check(const shared_state_t *state)
{
    size_t index;
    uint32_t generation;

    if (state->table_size != TABLE_SIZE) {
        return -1;
    }

    generation = state->table[0].generation;
    if (state->generation != generation) {
        return -1;
    }

    for (index = 0; index < TABLE_SIZE; ++index) {
        const record_t *record = &state->table[index];

        if (record->id != (uint32_t)index ||
            record->generation != generation ||
            record->value != generation * TABLE_SIZE + (uint32_t)index) {
            return -1;
        }
    }

    return 0;
}
