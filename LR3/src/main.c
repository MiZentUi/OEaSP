#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client.h"
#include "control.h"
#include "logging.h"
#include "manager.h"
#include "server.h"

static uint32_t parse_u32(const char *text, const char *name)
{
    char *end = NULL;
    unsigned long value;

    errno = 0;
    value = strtoul(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        value == 0 || value > UINT32_MAX) {
        fprintf(stderr, "invalid %s: %s\n", name, text);
        exit(2);
    }
    return (uint32_t)value;
}

static void usage(const char *program)
{
    fprintf(stderr,
            "usage:\n"
            "  %s run <RUN_ID>\n"
            "  %s bench <RUN_ID>\n"
            "  %s server <RUN_ID>\n"
            "  %s client <RUN_ID> <INDEX>\n"
            "  %s stop <RUN_ID>\n",
            program, program, program, program, program);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        usage(argv[0]);
        return 2;
    }

    logging_set_run_id(parse_u32(argv[2], "RUN_ID"));

    if (strcmp(argv[1], "run") == 0 && argc == 3) {
        set_role("MANAGER");
        return manager_main(false);
    }
    if (strcmp(argv[1], "bench") == 0 && argc == 3) {
        set_role("MANAGER");
        return manager_main(true);
    }
    if (strcmp(argv[1], "server") == 0 && argc == 3) {
        set_role("SERVER");
        return server_main();
    }
    if (strcmp(argv[1], "client") == 0 && argc == 4) {
        uint32_t index = parse_u32(argv[3], "CLIENT_INDEX");

        if (index > 99) {
            fprintf(stderr, "invalid CLIENT_INDEX: %s\n", argv[3]);
            return 2;
        }
        set_role("CLIENT#%u", index);
        return client_main(index);
    }
    if (strcmp(argv[1], "stop") == 0 && argc == 3) {
        set_role("STOPPER");
        return send_stop() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    usage(argv[0]);
    return 2;
}
