#ifndef LR5_NET_H
#define LR5_NET_H

#include <stddef.h>
#include <time.h>

#define NET_PEER_LEN 512

enum {
    NET_IO_ERROR = -1,
    NET_IO_TIMEOUT = -2,
    NET_IO_EOF = 0
};

int net_connect(const char *host, const char *port, int timeout_ms,
                int *fd_out, char *peer, size_t peer_size,
                char *error, size_t error_size);
int net_listen(const char *host, const char *port, int backlog,
               char *error, size_t error_size);
int net_accept(int listen_fd, int *fd_out, char *peer, size_t peer_size,
               char *error, size_t error_size);
int net_write_all(int fd, const void *data, size_t size,
                  const struct timespec *deadline,
                  char *error, size_t error_size);
int net_read_some(int fd, void *data, size_t size,
                  const struct timespec *deadline,
                  char *error, size_t error_size);
void net_close(int fd);
void net_set_error(char *error, size_t error_size,
                   const char *function, int code);

#endif
