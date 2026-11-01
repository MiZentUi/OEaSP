#ifndef MODEL_H
#define MODEL_H

#include "shared.h"

void model_init(shared_state_t *state);
void model_write(shared_state_t *state);
int model_check(const shared_state_t *state);

#endif
