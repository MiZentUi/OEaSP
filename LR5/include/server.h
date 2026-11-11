#ifndef LR5_SERVER_H
#define LR5_SERVER_H

#include <stdbool.h>
#include <stdio.h>

#define SERVER_HOST_MAX 256
#define SERVER_PORT_MAX 16

enum {
    SERVER_PARSE_OK = 0,
    SERVER_PARSE_HELP = 1,
    SERVER_PARSE_ERROR = 2
};

typedef struct {
    char host[SERVER_HOST_MAX];
    char port[SERVER_PORT_MAX];
    int timeout_ms;
    int slow_ms;
    int backlog;
    size_t max_request;
    bool quiet;
} server_options_t;

int server_parse_args(int argc, char **argv, server_options_t *options);
void server_usage(FILE *stream, const char *program);
int server_run(const server_options_t *options);

#endif
