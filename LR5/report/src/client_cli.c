#include "client.h"

#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef DEFAULT_PORT
#define DEFAULT_PORT 20103
#endif

enum {
    OPT_HOST = 1000,
    OPT_PORT,
    OPT_ID,
    OPT_TIMEOUT,
    OPT_COUNT,
    OPT_METHOD,
    OPT_PATH,
    OPT_OUTPUT,
    OPT_MAX_BODY,
    OPT_INTERVAL,
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

void client_usage(FILE *stream, const char *program)
{
    fprintf(stream,
            "usage: %s [options]\n"
            "\n"
            "options:\n"
            "  --host ADDRESS       server address (default 127.0.0.1)\n"
            "  --port PORT          server port (default %d)\n"
            "  --id N               participant identifier (default 1)\n"
            "  --timeout-ms N       operation timeout (default 5000)\n"
            "  --count N            number of requests (default 1)\n"
            "  --method GET|HEAD    HTTP method (default GET)\n"
            "  --path PATH          request target (default /)\n"
            "  --output FILE        write response body to FILE\n"
            "  --max-body N         response body limit in bytes (default 4194304)\n"
            "  --interval-ms N      delay between requests (default 0)\n"
            "  --quiet              disable the event journal\n"
            "  --help               show this help\n",
            program, DEFAULT_PORT);
}

int client_parse_args(int argc, char **argv, client_options_t *options)
{
    int option;
    int option_index = 0;
    static const struct option long_options[] = {
        {"host", required_argument, NULL, OPT_HOST},
        {"port", required_argument, NULL, OPT_PORT},
        {"id", required_argument, NULL, OPT_ID},
        {"timeout-ms", required_argument, NULL, OPT_TIMEOUT},
        {"count", required_argument, NULL, OPT_COUNT},
        {"method", required_argument, NULL, OPT_METHOD},
        {"path", required_argument, NULL, OPT_PATH},
        {"output", required_argument, NULL, OPT_OUTPUT},
        {"max-body", required_argument, NULL, OPT_MAX_BODY},
        {"interval-ms", required_argument, NULL, OPT_INTERVAL},
        {"quiet", no_argument, NULL, OPT_QUIET},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    memset(options, 0, sizeof(*options));
    snprintf(options->host, sizeof(options->host), "127.0.0.1");
    snprintf(options->port, sizeof(options->port), "%d", DEFAULT_PORT);
    options->participant_id = 1;
    options->timeout_ms = 5000;
    options->count = 1;
    snprintf(options->method, sizeof(options->method), "GET");
    snprintf(options->path, sizeof(options->path), "/");
    options->max_body = 4U * 1024U * 1024U;

    opterr = 0;
    while ((option = getopt_long(argc, argv, "h", long_options,
                                 &option_index)) != -1) {
        long value;

        switch (option) {
        case OPT_HOST:
            if (strlen(optarg) >= sizeof(options->host)) {
                fprintf(stderr, "ERROR host is too long\n");
                return CLIENT_PARSE_ERROR;
            }
            snprintf(options->host, sizeof(options->host), "%s", optarg);
            break;
        case OPT_PORT:
            if (parse_long("port", optarg, 1, 65535, &value) < 0) {
                return CLIENT_PARSE_ERROR;
            }
            snprintf(options->port, sizeof(options->port), "%ld", value);
            break;
        case OPT_ID:
            if (parse_long("id", optarg, 1, 1000000, &value) < 0) {
                return CLIENT_PARSE_ERROR;
            }
            options->participant_id = (int)value;
            break;
        case OPT_TIMEOUT:
            if (parse_long("timeout-ms", optarg, 1, 3600000, &value) < 0) {
                return CLIENT_PARSE_ERROR;
            }
            options->timeout_ms = (int)value;
            break;
        case OPT_COUNT:
            if (parse_long("count", optarg, 1, 1000000, &value) < 0) {
                return CLIENT_PARSE_ERROR;
            }
            options->count = (unsigned long)value;
            break;
        case OPT_METHOD:
            if (strcasecmp(optarg, "GET") != 0 &&
                strcasecmp(optarg, "HEAD") != 0) {
                fprintf(stderr, "ERROR method must be GET or HEAD\n");
                return CLIENT_PARSE_ERROR;
            }
            snprintf(options->method, sizeof(options->method), "%s",
                     strcasecmp(optarg, "HEAD") == 0 ? "HEAD" : "GET");
            break;
        case OPT_PATH:
            if (optarg[0] != '/' ||
                strlen(optarg) >= sizeof(options->path) ||
                strpbrk(optarg, " \t\r\n") != NULL) {
                fprintf(stderr, "ERROR invalid path: %s\n", optarg);
                return CLIENT_PARSE_ERROR;
            }
            snprintf(options->path, sizeof(options->path), "%s", optarg);
            break;
        case OPT_OUTPUT:
            if (strlen(optarg) >= sizeof(options->output)) {
                fprintf(stderr, "ERROR output path is too long\n");
                return CLIENT_PARSE_ERROR;
            }
            snprintf(options->output, sizeof(options->output), "%s", optarg);
            break;
        case OPT_MAX_BODY:
            if (parse_long("max-body", optarg, 0, 1073741824, &value) < 0) {
                return CLIENT_PARSE_ERROR;
            }
            options->max_body = (size_t)value;
            break;
        case OPT_INTERVAL:
            if (parse_long("interval-ms", optarg, 0, 3600000, &value) < 0) {
                return CLIENT_PARSE_ERROR;
            }
            options->interval_ms = (int)value;
            break;
        case OPT_QUIET:
            options->quiet = true;
            break;
        case 'h':
            client_usage(stdout, argv[0]);
            return CLIENT_PARSE_HELP;
        default:
            fprintf(stderr, "ERROR unknown option: %s\n",
                    argv[optind - 1]);
            client_usage(stderr, argv[0]);
            return CLIENT_PARSE_ERROR;
        }
    }

    if (optind < argc) {
        fprintf(stderr, "ERROR unexpected argument: %s\n", argv[optind]);
        client_usage(stderr, argv[0]);
        return CLIENT_PARSE_ERROR;
    }
    return CLIENT_PARSE_OK;
}
