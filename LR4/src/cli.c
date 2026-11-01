#include "cli.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "timing.h"

#define DEFAULT_OPERATIONS 5
#define DEFAULT_WRITE_US 100L
#define DEFAULT_READ_US 100L
#define DEFAULT_SEED 12345

enum {
    OPT_OPERATIONS = 1000,
    OPT_WRITE_US,
    OPT_READ_US
};

static void usage_to(FILE *stream, const char *program)
{
    fprintf(stream,
            "usage: %s [options]\n"
            "\n"
            "options:\n"
            "  --operations N       operations per process (default %d)\n"
            "  --write-us N         write duration in microseconds (default %ld)\n"
            "  --read-us N          read duration in microseconds (default %ld)\n"
            "  --help               show this help\n",
            program, DEFAULT_OPERATIONS, DEFAULT_WRITE_US, DEFAULT_READ_US);
}

void cli_usage(void)
{
    usage_to(stdout, "lr4");
}

static int parse_long(const char *name, const char *text, long minimum,
                      long maximum, long *value)
{
    char *end = NULL;
    long parsed;

    errno = 0;
    parsed = strtol(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed < minimum || parsed > maximum) {
        cli_error("ERROR invalid --%s value: %s", name, text);
        return -1;
    }

    *value = parsed;
    return 0;
}

int cli_parse(int argc, char **argv, cli_options_t *options)
{
    int option;
    int option_index = 0;

    static const struct option long_options[] = {
        {"operations", required_argument, NULL, OPT_OPERATIONS},
        {"write-us", required_argument, NULL, OPT_WRITE_US},
        {"read-us", required_argument, NULL, OPT_READ_US},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    options->operations = DEFAULT_OPERATIONS;
    options->write_us = DEFAULT_WRITE_US;
    options->read_us = DEFAULT_READ_US;
    options->seed = DEFAULT_SEED;

    opterr = 0;
    while ((option = getopt_long(argc, argv, "h", long_options,
                                 &option_index)) != -1) {
        long value;

        switch (option) {
        case OPT_OPERATIONS:
            if (parse_long("operations", optarg, 1, 1000000, &value) < 0) {
                return CLI_ERROR;
            }
            options->operations = (unsigned long)value;
            break;
        case OPT_WRITE_US:
            if (parse_long("write-us", optarg, 0, 10000000, &value) < 0) {
                return CLI_ERROR;
            }
            options->write_us = value;
            break;
        case OPT_READ_US:
            if (parse_long("read-us", optarg, 0, 10000000, &value) < 0) {
                return CLI_ERROR;
            }
            options->read_us = value;
            break;
        case 'h':
            cli_usage();
            return CLI_HELP;
        default:
            cli_error("ERROR unknown option: %s", argv[optind - 1]);
            usage_to(stderr, argv[0]);
            return CLI_ERROR;
        }
    }

    if (optind < argc) {
        cli_error("ERROR unexpected argument: %s", argv[optind]);
        usage_to(stderr, argv[0]);
        return CLI_ERROR;
    }

    return CLI_OK;
}

void cli_error(const char *format, ...)
{
    va_list arguments;

    va_start(arguments, format);
    vfprintf(stderr, format, arguments);
    va_end(arguments);
    fputc('\n', stderr);
    fflush(stderr);
}

void cli_report(const cli_options_t *options, const shared_state_t *state,
                bool table_consistent, int child_failures)
{
    uint64_t elapsed_ns = 0;
    uint64_t total_operations = 0;
    uint64_t total_records = 0;
    uint64_t expected_operations;
    int participants = WRITER_COUNT + READER_COUNT;
    int index;

    {
        struct timespec now;

        if ((state->start_time.tv_sec != 0 ||
             state->start_time.tv_nsec != 0) &&
            clock_gettime(CLOCK_MONOTONIC, &now) == 0 &&
            now.tv_sec >= state->start_time.tv_sec) {
            elapsed_ns = timing_diff_ns(&state->start_time, &now);
        }
    }

    for (index = 0; index < participants; ++index) {
        const worker_stats_t *stats = &state->stats[index];

        total_operations += stats->operations;
        total_records += stats->records;
    }
    expected_operations = options->operations * (uint64_t)participants;

    printf("\n");
    printf("=== LR4 basic writers-readers ===\n");
    printf("writers=%d readers=%d records=%u operations=%lu\n",
           WRITER_COUNT, READER_COUNT, state->table_size,
           options->operations);
    printf("%-7s %-3s %-7s %-10s %-10s\n", "ROLE", "ID", "PID", "OPS",
           "RECORDS");

    for (index = 0; index < participants; ++index) {
        const worker_stats_t *stats = &state->stats[index];

        printf("%-7s %-3d %-7d %-10" PRIu64 " %-10" PRIu64 "\n",
               stats->role == ROLE_WRITER ? "WRITER" : "READER",
               stats->index, stats->pid, stats->operations, stats->records);
    }

    printf("elapsed_ms=%.3f throughput_ops_s=%.3f\n",
           (double)elapsed_ns / 1000000.0,
           elapsed_ns == 0 ? 0.0
                           : (double)total_operations /
                                 ((double)elapsed_ns / 1000000000.0));
    printf("operations=%" PRIu64 "/%" PRIu64 " records=%" PRIu64
           " generation=%u\n",
           total_operations, expected_operations, total_records,
           state->generation);
    printf("table=%s child_failures=%d\n",
           table_consistent ? "consistent" : "inconsistent", child_failures);
    printf("RESULT: %s\n",
           table_consistent && child_failures == 0 ? "OK" : "FAILED");
    fflush(stdout);
}
