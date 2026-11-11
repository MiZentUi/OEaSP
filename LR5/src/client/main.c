#include <stdlib.h>

#include "client.h"

int main(int argc, char **argv)
{
    client_options_t options;
    int result = client_parse_args(argc, argv, &options);

    if (result == CLIENT_PARSE_HELP) {
        return EXIT_SUCCESS;
    }
    if (result != CLIENT_PARSE_OK) {
        return EXIT_FAILURE;
    }
    return client_run(&options);
}
