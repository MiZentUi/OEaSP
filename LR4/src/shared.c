#include "shared.h"

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static int map_shared_fd(int fd, shared_state_t **out)
{
    struct stat info;
    void *address;

    if (fstat(fd, &info) < 0) {
        return -1;
    }
    if ((size_t)info.st_size < sizeof(shared_state_t)) {
        errno = EINVAL;
        return -1;
    }

    address = mmap(NULL, sizeof(shared_state_t), PROT_READ | PROT_WRITE,
                   MAP_SHARED, fd, 0);
    if (address == MAP_FAILED) {
        return -1;
    }

    *out = address;
    return 0;
}

int shared_create(const char *name, shared_state_t **out)
{
    int fd;
    int saved;
    shared_state_t *state = NULL;

    fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
    if (fd < 0) {
        return -1;
    }

    if (ftruncate(fd, (off_t)sizeof(shared_state_t)) < 0) {
        saved = errno;
        close(fd);
        shm_unlink(name);
        errno = saved;
        return -1;
    }

    if (map_shared_fd(fd, &state) < 0) {
        saved = errno;
        close(fd);
        shm_unlink(name);
        errno = saved;
        return -1;
    }

    close(fd);
    *out = state;
    return 0;
}

int shared_attach(const char *name, shared_state_t **out)
{
    int fd;
    int saved;

    fd = shm_open(name, O_RDWR, 0);
    if (fd < 0) {
        return -1;
    }

    if (map_shared_fd(fd, out) < 0) {
        saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    close(fd);
    return 0;
}

int shared_detach(shared_state_t *state)
{
    if (state == NULL) {
        return -1;
    }
    return munmap(state, sizeof(shared_state_t));
}

int shared_remove(const char *name)
{
    return shm_unlink(name);
}

int shared_state_init(shared_state_t *state)
{
    memset(state, 0, sizeof(*state));
    state->table_size = TABLE_SIZE;
    state->magic = SHARED_MAGIC;
    return 0;
}

void shared_state_destroy(shared_state_t *state)
{
    (void)state;
}
