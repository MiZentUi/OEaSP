#include "cli.h"

#include <errno.h>
#include <getopt.h>
#include <inttypes.h>
#include <limits.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "timing.h"

#ifndef STUDENT_ID
#define STUDENT_ID 103
#endif

#ifndef BASE_OPERATIONS
#define BASE_OPERATIONS 20
#endif

#define DEFAULT_WRITERS 2
#define DEFAULT_READERS 2
#define DEFAULT_WRITE_US 100L
#define DEFAULT_READ_US 100L
#define DEFAULT_TIMEOUT_MS 5000
#define DEFAULT_DURATION_MS 60000
#define DEFAULT_SEED 1
#define STUDENT_SEED (1000 + STUDENT_ID * 97)
#define INDIVIDUAL_OPERATIONS (BASE_OPERATIONS + STUDENT_ID * 5)

enum {
    OPT_WRITERS = 1000,
    OPT_READERS,
    OPT_OPERATIONS,
    OPT_WRITE_US,
    OPT_READ_US,
    OPT_TIMEOUT_MS,
    OPT_DURATION_MS,
    OPT_SEED,
    OPT_INDIVIDUAL,
    OPT_QUIET
};

static shared_state_t *g_state;
static bool g_quiet;

static void usage_to(FILE *stream, const char *program)
{
    fprintf(stream,
            "usage: %s [options]\n"
            "\n"
            "options:\n"
            "  --writers N          writer process count (default %d)\n"
            "  --readers N          reader process count (default %d)\n"
            "  --operations N       operations per process (default %d)\n"
            "  --write-us N         write duration in microseconds (default %ld)\n"
            "  --read-us N          read duration in microseconds (default %ld)\n"
            "  --timeout-ms N       access timeout (default %d)\n"
            "  --max-duration-ms N  experiment limit (default %d)\n"
            "  --seed N             random seed (default %d)\n"
            "  --individual         use STUDENT_ID seed and operation count\n"
            "  --quiet              disable the event journal\n"
            "  --help               show this help\n",
            program, DEFAULT_WRITERS, DEFAULT_READERS, BASE_OPERATIONS,
            DEFAULT_WRITE_US, DEFAULT_READ_US, DEFAULT_TIMEOUT_MS,
            DEFAULT_DURATION_MS, DEFAULT_SEED);
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
    bool operations_set = false;
    bool seed_set = false;
    int option;
    int option_index = 0;

    static const struct option long_options[] = {
        {"writers", required_argument, NULL, OPT_WRITERS},
        {"readers", required_argument, NULL, OPT_READERS},
        {"operations", required_argument, NULL, OPT_OPERATIONS},
        {"write-us", required_argument, NULL, OPT_WRITE_US},
        {"read-us", required_argument, NULL, OPT_READ_US},
        {"timeout-ms", required_argument, NULL, OPT_TIMEOUT_MS},
        {"max-duration-ms", required_argument, NULL, OPT_DURATION_MS},
        {"seed", required_argument, NULL, OPT_SEED},
        {"individual", no_argument, NULL, OPT_INDIVIDUAL},
        {"quiet", no_argument, NULL, OPT_QUIET},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}
    };

    options->writers = DEFAULT_WRITERS;
    options->readers = DEFAULT_READERS;
    options->operations = BASE_OPERATIONS;
    options->write_us = DEFAULT_WRITE_US;
    options->read_us = DEFAULT_READ_US;
    options->timeout_ms = DEFAULT_TIMEOUT_MS;
    options->max_duration_ms = DEFAULT_DURATION_MS;
    options->seed = DEFAULT_SEED;
    options->quiet = false;
    options->individual = false;

    opterr = 0;
    while ((option = getopt_long(argc, argv, "h", long_options,
                                 &option_index)) != -1) {
        long value;

        switch (option) {
        case OPT_WRITERS:
            if (parse_long("writers", optarg, 1, MAX_PARTICIPANTS,
                           &value) < 0) {
                return CLI_ERROR;
            }
            options->writers = (int)value;
            break;
        case OPT_READERS:
            if (parse_long("readers", optarg, 1, MAX_PARTICIPANTS,
                           &value) < 0) {
                return CLI_ERROR;
            }
            options->readers = (int)value;
            break;
        case OPT_OPERATIONS:
            if (parse_long("operations", optarg, 1, 1000000, &value) < 0) {
                return CLI_ERROR;
            }
            options->operations = (unsigned long)value;
            operations_set = true;
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
        case OPT_TIMEOUT_MS:
            if (parse_long("timeout-ms", optarg, 1, 3600000, &value) < 0) {
                return CLI_ERROR;
            }
            options->timeout_ms = (int)value;
            break;
        case OPT_DURATION_MS:
            if (parse_long("max-duration-ms", optarg, 1, 3600000,
                           &value) < 0) {
                return CLI_ERROR;
            }
            options->max_duration_ms = (int)value;
            break;
        case OPT_SEED:
            if (parse_long("seed", optarg, 0, (long)UINT32_MAX, &value) < 0) {
                return CLI_ERROR;
            }
            options->seed = (unsigned int)value;
            seed_set = true;
            break;
        case OPT_INDIVIDUAL:
            options->individual = true;
            break;
        case OPT_QUIET:
            options->quiet = true;
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

    if (options->individual && !operations_set) {
        options->operations = INDIVIDUAL_OPERATIONS;
    }
    if (options->individual && !seed_set) {
        options->seed = STUDENT_SEED;
    }

    if (options->writers + options->readers > MAX_PARTICIPANTS) {
        cli_error("ERROR too many participants: %d",
                  options->writers + options->readers);
        return CLI_ERROR;
    }

    return CLI_OK;
}

void cli_bind(shared_state_t *state, bool quiet)
{
    g_state = state;
    g_quiet = quiet;
}

static uint64_t elapsed_ms(const shared_state_t *state)
{
    struct timespec now;

    if (state == NULL ||
        (state->start_time.tv_sec == 0 && state->start_time.tv_nsec == 0)) {
        return 0;
    }
    if (clock_gettime(CLOCK_MONOTONIC, &now) < 0) {
        return 0;
    }
    if (now.tv_sec < state->start_time.tv_sec) {
        return 0;
    }
    return timing_diff_ns(&state->start_time, &now) / 1000000ULL;
}

static bool lock_journal(void)
{
    int rc;

    if (g_state == NULL) {
        return false;
    }

    rc = pthread_mutex_lock(&g_state->journal_mutex);
    if (rc == EOWNERDEAD) {
        atomic_fetch_add(&g_state->invariant_errors, 1);
        if (pthread_mutex_consistent(&g_state->journal_mutex) != 0) {
            pthread_mutex_unlock(&g_state->journal_mutex);
            return false;
        }
        return true;
    }
    return rc == 0;
}

static void write_console(int fd, const char *line, int length)
{
    size_t size;

    if (length <= 0) {
        return;
    }
    size = (size_t)length;
    if (size > 511) {
        size = 511;
    }
    (void)write(fd, line, size);
}

void cli_event(const char *format, ...)
{
    char message[384];
    char line[512];
    va_list arguments;
    int length;
    bool locked;

    if (g_state == NULL || g_quiet) {
        return;
    }

    // one locked write keeps journal lines intact across processes
    locked = lock_journal();
    if (!locked) {
        return;
    }

    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    length = snprintf(line, sizeof(line), "[%06" PRIu64 " ms] %s\n",
                      elapsed_ms(g_state), message);
    write_console(STDOUT_FILENO, line, length);
    pthread_mutex_unlock(&g_state->journal_mutex);
}

void cli_error(const char *format, ...)
{
    char message[384];
    char line[512];
    va_list arguments;
    int length;
    bool locked = false;

    va_start(arguments, format);
    vsnprintf(message, sizeof(message), format, arguments);
    va_end(arguments);

    if (g_state != NULL) {
        locked = lock_journal();
    }

    if (g_state != NULL && locked) {
        length = snprintf(line, sizeof(line), "[%06" PRIu64 " ms] %s\n",
                          elapsed_ms(g_state), message);
    } else {
        length = snprintf(line, sizeof(line), "%s\n", message);
    }
    write_console(STDERR_FILENO, line, length);

    if (locked) {
        pthread_mutex_unlock(&g_state->journal_mutex);
    }
}

void cli_report(const cli_options_t *options, const shared_state_t *state,
                bool aborted, int child_failures)
{
    uint64_t elapsed_ns;
    uint64_t total_operations = 0;
    uint64_t total_timeouts = 0;
    uint64_t total_records = 0;
    uint64_t expected_operations;
    unsigned int violations;
    int participants = options->writers + options->readers;
    int index;

    {
        struct timespec now;

        if ((state->start_time.tv_sec != 0 ||
             state->start_time.tv_nsec != 0) &&
            clock_gettime(CLOCK_MONOTONIC, &now) == 0 &&
            now.tv_sec >= state->start_time.tv_sec) {
            elapsed_ns = timing_diff_ns(&state->start_time, &now);
        } else {
            elapsed_ns = 0;
        }
    }

    for (index = 0; index < participants; ++index) {
        const worker_stats_t *stats = &state->stats[index];

        total_operations += stats->operations;
        total_timeouts += stats->timeouts;
        total_records += stats->records;
    }
    expected_operations = options->operations * (uint64_t)participants;

    printf("\n");
    printf("=== LR4 interprocess writers-readers ===\n");
    printf("mode=%s writers=%d readers=%d records=%u operations=%lu seed=%u\n",
           options->individual ? "individual" : "standard",
           options->writers, options->readers, state->table_size,
           options->operations, options->seed);
    printf("%-7s %-3s %-7s %-10s %-10s %-12s %-12s %-10s\n",
           "ROLE", "ID", "PID", "OPS", "TIMEOUTS", "AVG_WAIT_MS",
           "MAX_WAIT_MS", "RECORDS");

    for (index = 0; index < participants; ++index) {
        const worker_stats_t *stats = &state->stats[index];
        uint64_t attempts = stats->operations + stats->timeouts;
        double average_wait = attempts == 0
                                  ? 0.0
                                  : (double)stats->wait_ns /
                                        (double)attempts / 1000000.0;

        printf("%-7s %-3d %-7d %-10" PRIu64 " %-10" PRIu64
               " %-12.3f %-12.3f %-10" PRIu64 "\n",
               stats->role == ROLE_WRITER ? "WRITER" : "READER",
               stats->index, stats->pid, stats->operations,
               stats->timeouts, average_wait,
               (double)stats->max_wait_ns / 1000000.0, stats->records);
    }

    violations = atomic_load(&state->invariant_errors);
    printf("elapsed_ms=%.3f throughput_ops_s=%.3f\n",
           (double)elapsed_ns / 1000000.0,
           elapsed_ns == 0 ? 0.0
                           : (double)total_operations /
                                 ((double)elapsed_ns / 1000000000.0));
    printf("operations=%" PRIu64 "/%" PRIu64 " timeouts=%" PRIu64
           " records=%" PRIu64 "\n",
           total_operations, expected_operations, total_timeouts,
           total_records);
    printf("peak_readers=%d peak_writers=%d generation=%u\n",
           atomic_load(&state->peak_readers),
           atomic_load(&state->peak_writers), state->generation);
    printf("invariant_violations=%u child_failures=%d aborted=%s\n",
           violations, child_failures, aborted ? "yes" : "no");
    printf("RESULT: %s\n",
           !aborted && child_failures == 0 && violations == 0 ? "OK"
                                                             : "FAILED");
    fflush(stdout);
}
