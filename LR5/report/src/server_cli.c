#include "server.h"

#include <errno.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef DEFAULT_PORT
#define DEFAULT_PORT 20103
#endif

enum {
    OPT_HOST = 1000,
    OPT_PORT,
    OPT_TIMEOUT,
    OPT_SLOW,
    OPT_BACKLOG,
    OPT_MAX_REQUEST,
    OPT_QUIET
};

static int parse_long(const char *name, const char *text,
                      long minimum, long maximum, long *value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        fprintf(stderr, "ERROR invalid --%s value: %s\n", name, text);
        return -1;
    }
    *value = parsed;
    return 0;
}

void server_usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "usage: %s [options]\n"
            "\n"
            "options:\n"
            "  --host ADDRESS       listen address (default 127.0.0.1)\n"
            "  --port PORT          listen port (default %d)\n"
            "  --timeout-ms N       client I/O timeout (default 5000)\n"
            "  --slow-ms N          delay before /slow body (default 50)\n"
            "  --backlog N          listen backlog (default 16)\n"
            "  --max-request N      request header limit (default 8192)\n"
            "  --quiet              disable the event journal\n"
            "  --help               show this help\n",
            program, DEFAULT_PORT);
}

int server_parse_args(int argc, char **argv, server_options_t *options)
{
    int option;
    int option_index = 0;
    static const struct option long_options[] = {
        {"host", required_argument, NULL, OPT_HOST},
        {"port", required_argument, NULL, OPT_PORT},
        {"timeout-ms", required_argument, NULL, OPT_TIMEOUT},
        {"slow-ms", required_argument, NULL, OPT_SLOW},
        {"backlog", required_argument, NULL, OPT_BACKLOG},
        {"max-request", required_argument, NULL, OPT_MAX_REQUEST},
        {"quiet", no_argument, NULL, OPT_QUIET},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    memset(options, 0, sizeof(*options));
    snprintf(options->host, sizeof(options->host), "127.0.0.1");
    snprintf(options->port, sizeof(options->port), "%d", DEFAULT_PORT);
    options->timeout_ms = 5000;
    options->slow_ms = 50;
    options->backlog = 16;
    options->max_request = 8192;

    opterr = 0;
    while ((option = getopt_long(argc, argv, "h", long_options,
                                 &option_index)) != -1) {
        long value;

        switch (option) {
        case OPT_HOST:
            if (strlen(optarg) >= sizeof(options->host)) {
                fprintf(stderr, "ERROR host is too long\n");
                return SERVER_PARSE_ERROR;
            }
            snprintf(options->host, sizeof(options->host), "%s", optarg);
            break;
        case OPT_PORT:
            if (parse_long("port", optarg, 1, 65535, &value) < 0) {
                return SERVER_PARSE_ERROR;
            }
            snprintf(options->port, sizeof(options->port), "%ld", value);
            break;
        case OPT_TIMEOUT:
            if (parse_long("timeout-ms", optarg, 1, 3600000, &value) < 0) {
                return SERVER_PARSE_ERROR;
            }
            options->timeout_ms = (int)value;
            break;
        case OPT_SLOW:
            if (parse_long("slow-ms", optarg, 0, 3600000, &value) < 0) {
                return SERVER_PARSE_ERROR;
            }
            options->slow_ms = (int)value;
            break;
        case OPT_BACKLOG:
            if (parse_long("backlog", optarg, 1, 1024, &value) < 0) {
                return SERVER_PARSE_ERROR;
            }
            options->backlog = (int)value;
            break;
        case OPT_MAX_REQUEST:
            if (parse_long("max-request", optarg, 256, 1048576, &value) < 0) {
                return SERVER_PARSE_ERROR;
            }
            options->max_request = (size_t)value;
            break;
        case OPT_QUIET:
            options->quiet = true;
            break;
        case 'h':
            server_usage(stdout, argv[0]);
            return SERVER_PARSE_HELP;
        default:
            fprintf(stderr, "ERROR unknown option: %s\n",
                    argv[optind - 1]);
            server_usage(stderr, argv[0]);
            return SERVER_PARSE_ERROR;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "ERROR unexpected argument: %s\n", argv[optind]);
        server_usage(stderr, argv[0]);
        return SERVER_PARSE_ERROR;
    }
    return SERVER_PARSE_OK;
}
