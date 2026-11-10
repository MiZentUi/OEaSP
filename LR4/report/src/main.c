#include <stdlib.h>

#include "cli.h"
#include "manager.h"

int main(int argc, char **argv)
{
    cli_options_t options;
    int rc;

    rc = cli_parse(argc, argv, &options);
    if (rc == CLI_HELP) {
        return EXIT_SUCCESS;
    }
    if (rc != CLI_OK) {
        return EXIT_FAILURE;
    }

    return manager_run(&options) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
