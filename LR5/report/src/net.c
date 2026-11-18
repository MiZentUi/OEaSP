#include "net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "timing.h"

void net_set_error(char *error, size_t error_size,
                   const char *function, int code)
{
    if (error_size == 0) {
        return;
    }
    snprintf(error, error_size, "%s: %s (%d)",
             function, code != 0 ? strerror(code) : "unknown error", code);
}

static int set_nonblocking(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);

    if (flags < 0) {
        return -1;
    }
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void format_peer(const struct sockaddr *address, socklen_t length,
                        char *peer, size_t peer_size)
{
    char host[NI_MAXHOST];
    char service[NI_MAXSERV];

    if (peer_size == 0) {
        return;
    }
    if (getnameinfo(address, length, host, sizeof(host),
                    service, sizeof(service),
                    NI_NUMERICHOST | NI_NUMERICSERV) == 0) {
        snprintf(peer, peer_size, "%s:%s", host, service);
    } else {
        snprintf(peer, peer_size, "unknown");
    }
}

static int wait_fd(int fd, short events, const struct timespec *deadline,
                   char *error, size_t error_size)
{
    for (;;) {
        struct pollfd descriptor;
        int remaining;
        int result;

        remaining = timing_remaining_ms(deadline);
        if (remaining <= 0) {
            snprintf(error, error_size, "poll: timeout");
            return NET_IO_TIMEOUT;
        }
        descriptor.fd = fd;
        descriptor.events = events;
        descriptor.revents = 0;
        result = poll(&descriptor, 1, remaining);
        if (result > 0) {
            return 0;
        }
        if (result == 0) {
            snprintf(error, error_size, "poll: timeout");
            return NET_IO_TIMEOUT;
        }
        if (errno == EINTR) {
            continue;
        }
        net_set_error(error, error_size, "poll", errno);
        return NET_IO_ERROR;
    }
}

int net_connect(const char *host, const char *port, int timeout_ms,
                int *fd_out, char *peer, size_t peer_size,
                char *error, size_t error_size)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    struct timespec deadline;
    int last_error = 0;
    int status;
    bool timed_out = false;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    status = getaddrinfo(host, port, &hints, &addresses);
    if (status != 0) {
        snprintf(error, error_size, "getaddrinfo: %s", gai_strerror(status));
        return NET_IO_ERROR;
    }

    timing_deadline_from_ms(timeout_ms, &deadline);
    for (address = addresses; address != NULL; address = address->ai_next) {
        int fd;
        int result;

        fd = socket(address->ai_family, address->ai_socktype,
                    address->ai_protocol);
        if (fd < 0) {
            last_error = errno;
            continue;
        }
        if (set_nonblocking(fd) < 0) {
            last_error = errno;
            close(fd);
            continue;
        }

        result = connect(fd, address->ai_addr, address->ai_addrlen);
        if (result < 0 && errno != EINPROGRESS) {
            last_error = errno;
            close(fd);
            continue;
        }
        if (result < 0) {
            result = wait_fd(fd, POLLOUT, &deadline, error, error_size);
            if (result == NET_IO_TIMEOUT) {
                timed_out = true;
                close(fd);
                continue;
            }
            if (result < 0) {
                close(fd);
                continue;
            }
            {
                int socket_error = 0;
                socklen_t length = sizeof(socket_error);

                if (getsockopt(fd, SOL_SOCKET, SO_ERROR,
                               &socket_error, &length) < 0) {
                    last_error = errno;
                    close(fd);
                    continue;
                }
                if (socket_error != 0) {
                    last_error = socket_error;
                    close(fd);
                    continue;
                }
            }
        }

        *fd_out = fd;
        format_peer(address->ai_addr, address->ai_addrlen, peer, peer_size);
        freeaddrinfo(addresses);
        return 0;
    }

    freeaddrinfo(addresses);
    if (timed_out) {
        snprintf(error, error_size, "connect: timeout");
        return NET_IO_TIMEOUT;
    }
    net_set_error(error, error_size, "connect", last_error);
    return NET_IO_ERROR;
}

int net_listen(const char *host, const char *port, int backlog,
               char *error, size_t error_size)
{
    struct addrinfo hints;
    struct addrinfo *addresses = NULL;
    struct addrinfo *address;
    int last_error = 0;
    int status;

    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    status = getaddrinfo(host != NULL && host[0] != '\0' ? host : NULL,
                         port, &hints, &addresses);
    if (status != 0) {
        snprintf(error, error_size, "getaddrinfo: %s", gai_strerror(status));
        return NET_IO_ERROR;
    }

    for (address = addresses; address != NULL; address = address->ai_next) {
        int enabled = 1;
        int fd;

        fd = socket(address->ai_family, address->ai_socktype,
                    address->ai_protocol);
        if (fd < 0) {
            last_error = errno;
            continue;
        }
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR,
                       &enabled, sizeof(enabled)) < 0) {
            last_error = errno;
            close(fd);
            continue;
        }
        if (bind(fd, address->ai_addr, address->ai_addrlen) < 0) {
            last_error = errno;
            close(fd);
            continue;
        }
        if (listen(fd, backlog) < 0) {
            last_error = errno;
            close(fd);
            continue;
        }
        freeaddrinfo(addresses);
        return fd;
    }

    freeaddrinfo(addresses);
    net_set_error(error, error_size, "bind/listen", last_error);
    return NET_IO_ERROR;
}

int net_accept(int listen_fd, int *fd_out, char *peer, size_t peer_size,
               char *error, size_t error_size)
{
    struct sockaddr_storage address;
    socklen_t length = sizeof(address);
    int fd;

    fd = accept(listen_fd, (struct sockaddr *)&address, &length);
    if (fd < 0) {
        net_set_error(error, error_size, "accept", errno);
        return NET_IO_ERROR;
    }
    if (set_nonblocking(fd) < 0) {
        net_set_error(error, error_size, "fcntl", errno);
        close(fd);
        return NET_IO_ERROR;
    }
    format_peer((struct sockaddr *)&address, length, peer, peer_size);
    *fd_out = fd;
    return 0;
}

int net_write_all(int fd, const void *data, size_t size,
                  const struct timespec *deadline,
                  char *error, size_t error_size)
{
    const char *bytes = data;
    size_t sent = 0;

    while (sent < size) {
        ssize_t result;
        int wait_result = wait_fd(fd, POLLOUT, deadline, error, error_size);

        if (wait_result < 0) {
            return wait_result;
        }
        result = send(fd, bytes + sent, size - sent, MSG_NOSIGNAL);
        if (result > 0) {
            sent += (size_t)result;
            continue;
        }
        if (result < 0 && (errno == EINTR || errno == EAGAIN ||
                           errno == EWOULDBLOCK)) {
            continue;
        }
        if (result == 0) {
            snprintf(error, error_size, "send: connection closed");
        } else {
            net_set_error(error, error_size, "send", errno);
        }
        return NET_IO_ERROR;
    }
    return 0;
}

int net_read_some(int fd, void *data, size_t size,
                  const struct timespec *deadline,
                  char *error, size_t error_size)
{
    for (;;) {
        ssize_t result;
        int wait_result = wait_fd(fd, POLLIN, deadline, error, error_size);

        if (wait_result < 0) {
            return wait_result;
        }
        result = recv(fd, data, size, 0);
        if (result > 0) {
            return (int)result;
        }
        if (result == 0) {
            return NET_IO_EOF;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
            continue;
        }
        net_set_error(error, error_size, "recv", errno);
        return NET_IO_ERROR;
    }
}

void net_close(int fd)
{
    if (fd >= 0) {
        close(fd);
    }
}
