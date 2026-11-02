#ifndef SHARED_H
#define SHARED_H

#include <pthread.h>
#include <semaphore.h>
#include <stdatomic.h>
#include <stdint.h>
#include <time.h>

#define TABLE_SIZE 100
#define MAX_PARTICIPANTS 32
#define SHARED_MAGIC 0x4c523400u

typedef enum {
    ROLE_WRITER = 0,
    ROLE_READER = 1
} worker_role_t;

typedef struct {
    uint32_t id;
    uint32_t generation;
    uint32_t value;
} record_t;

typedef struct {
    worker_role_t role;
    int index;
    int pid;
    uint64_t operations;
    uint64_t timeouts;
    uint64_t wait_ns;
    uint64_t max_wait_ns;
    uint64_t records;
} worker_stats_t;

typedef struct shared_state {
    uint32_t magic;
    uint32_t table_size;
    uint32_t generation;
    record_t table[TABLE_SIZE];

    pthread_mutex_t readers_mutex;
    pthread_mutex_t journal_mutex;
    sem_t turnstile;
    sem_t access_gate;
    sem_t start_gate;
    int readers_count;

    atomic_int active_readers;
    atomic_int active_writers;
    atomic_int peak_readers;
    atomic_int peak_writers;
    atomic_uint invariant_errors;

    struct timespec start_time;
    worker_stats_t stats[MAX_PARTICIPANTS];
} shared_state_t;

int shared_create(const char *name, shared_state_t **out);
int shared_attach(const char *name, shared_state_t **out);
int shared_detach(shared_state_t *state);
int shared_remove(const char *name);
int shared_state_init(shared_state_t *state);
void shared_state_destroy(shared_state_t *state);

#endif
