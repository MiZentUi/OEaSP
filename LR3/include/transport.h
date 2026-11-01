#ifndef TRANSPORT_H
#define TRANSPORT_H

#include <stddef.h>
#include <sys/socket.h>
#include <sys/un.h>

void make_socket_path(char *out, size_t size);
void fill_address(struct sockaddr_un *addr, const char *path);
socklen_t address_length(const struct sockaddr_un *addr);
int send_datagram(int fd, const struct sockaddr_un *addr,
                  const void *data, size_t size);

#endif
