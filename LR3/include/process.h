#ifndef PROCESS_H
#define PROCESS_H

#include <sys/types.h>

int wait_for_socket_path(pid_t pid, const char *path);
int wait_for_child(pid_t pid, const char *role);

#endif
