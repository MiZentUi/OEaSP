#ifndef LR5_CLIENT_H
#define LR5_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#define CLIENT_HOST_MAX 256
#define CLIENT_PORT_MAX 16
#define CLIENT_PATH_MAX 2048
#define CLIENT_OUTPUT_MAX 4096

enum {
    CLIENT_PARSE_OK = 0,
    CLIENT_PARSE_HELP = 1,
    CLIENT_PARSE_ERROR = 2
};

typedef struct {
    char host[CLIENT_HOST_MAX];
    char port[CLIENT_PORT_MAX];
    int participant_id;
    int timeout_ms;
    unsigned long count;
    char method[8];
    char path[CLIENT_PATH_MAX];
    char output[CLIENT_OUTPUT_MAX];
    size_t max_body;
    int interval_ms;
    bool quiet;
} client_options_t;

int client_parse_args(int argc, char **argv, client_options_t *options);
void client_usage(FILE *stream, const char *program);
int client_run(const client_options_t *options);

#endif
