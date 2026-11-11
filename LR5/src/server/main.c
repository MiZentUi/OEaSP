#include <stdlib.h>

#include "server.h"

int main(int argc, char **argv)
{
    server_options_t options;
    int result = server_parse_args(argc, argv, &options);

    if (result == SERVER_PARSE_HELP) {
        return EXIT_SUCCESS;
    }
    if (result != SERVER_PARSE_OK) {
        return EXIT_FAILURE;
    }
    return server_run(&options);
}
