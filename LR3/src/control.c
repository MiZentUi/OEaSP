#include "control.h"

#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include "logging.h"
#include "protocol.h"
#include "transport.h"

int send_stop(void)
{
    char path[108];
    struct sockaddr_un address;
    MessageHeader header;
    int fd;

    make_socket_path(path, sizeof(path));
    fill_address(&address, path);

    memset(&header, 0, sizeof(header));
    header.version = MSG_VERSION;
    header.messageType = MSG_STOP;
    header.sourcePid = (uint32_t)getpid();
    header.requestId = 999999;
    header.payloadSize = 0;

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        int error = errno;

        journal_error("ERROR func=socket errno=%d (%s)", error, strerror(error));
        return -1;
    }

    if (send_datagram(fd, &address, &header, sizeof(header)) < 0) {
        int error = errno;

        journal_error("ERROR func=sendto type=STOP errno=%d (%s)",
                      error, strerror(error));
        close(fd);
        return -1;
    }

    journal("SEND requestId=%u type=STOP size=%zu",
            header.requestId, sizeof(header));
    close(fd);
    return 0;
}
