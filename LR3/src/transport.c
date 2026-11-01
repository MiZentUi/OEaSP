#include "transport.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "logging.h"

void make_socket_path(char *out, size_t size)
{
    int written = snprintf(out, size, "/tmp/oeasp_lr3_%u.sock",
                           logging_run_id());

    if (written < 0 || (size_t)written >= size) {
        journal_error("ERROR socket path is too long");
        exit(EXIT_FAILURE);
    }
}

void fill_address(struct sockaddr_un *addr, const char *path)
{
    size_t length = strlen(path);

    memset(addr, 0, sizeof(*addr));
    addr->sun_family = AF_UNIX;
    if (length >= sizeof(addr->sun_path)) {
        journal_error("ERROR socket path is too long");
        exit(EXIT_FAILURE);
    }
    memcpy(addr->sun_path, path, length + 1);
}

// calculates sockaddr_un length from the socket path
socklen_t address_length(const struct sockaddr_un *addr)
{
    return (socklen_t)(offsetof(struct sockaddr_un, sun_path) +
                       strlen(addr->sun_path) + 1);
}

int send_datagram(int fd, const struct sockaddr_un *addr,
                  const void *data, size_t size)
{
    ssize_t sent = sendto(fd, data, size, 0,
                          (const struct sockaddr *)addr,
                          address_length(addr));

    if (sent < 0) {
        return -1;
    }
    if ((size_t)sent != size) {
        errno = EMSGSIZE;
        return -1;
    }
    return 0;
}
