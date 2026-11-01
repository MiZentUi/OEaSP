#include <stdlib.h>

#include "manager.h"

int main(void)
{
    return manager_run() == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
