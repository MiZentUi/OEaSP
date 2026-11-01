#include "manager.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cli.h"
#include "model.h"
#include "shared.h"

#ifndef STUDENT_ID
#define STUDENT_ID 103
#endif

static void make_shared_name(char *buffer, size_t size)
{
    snprintf(buffer, size, "/ossp_lr4_%d_%ld", STUDENT_ID, (long)getpid());
}

static bool table_is_consistent(shared_state_t *state)
{
    bool consistent = true;

    if (state->magic != SHARED_MAGIC) {
        cli_error("ERROR shared magic is invalid");
        consistent = false;
    }

    if (model_check(state) != 0) {
        cli_error("ERROR table is inconsistent");
        consistent = false;
    }

    return consistent;
}

int manager_run(void)
{
    char name[64] = "";
    shared_state_t *state = NULL;
    bool initialized = false;
    bool table_consistent = false;
    int result = 1;

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

    table_consistent = table_is_consistent(state);
    cli_report(state, table_consistent);
    result = table_consistent ? 0 : 1;

cleanup:
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
