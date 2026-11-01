#include "server.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include "logging.h"
#include "protocol.h"
#include "server_handlers.h"
#include "transport.h"

static const char *message_type_name(uint32_t type)
{
    switch (type) {
    case MSG_LOG:
        return "LOG";
    case MSG_STOP:
        return "STOP";
    case MSG_BENCH:
        return "BENCH";
    case MSG_BENCH_END:
        return "BENCH_END";
    default:
        return NULL;
    }
}

// creates the socket, sets the timeout and binds it to the path
static int open_server_socket(const char *path)
{
    struct sockaddr_un address;
    struct timeval timeout;
    int fd;

    fill_address(&address, path);

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        fatal_errno("socket");
    }

    timeout.tv_sec = SERVER_TIMEOUT_SEC;
    timeout.tv_usec = 0;
    if (setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                   &timeout, sizeof(timeout)) < 0) {
        close(fd);
        fatal_errno("setsockopt");
    }

    if (bind(fd, (struct sockaddr *)&address, address_length(&address)) < 0) {
        close(fd);
        fatal_errno("bind");
    }
    return fd;
}

// dispatches a message to its handler by type
static void dispatch_message(const unsigned char *datagram,
                             const MessageHeader *header,
                             uint32_t payload_size, FILE *log_file,
                             bool *stopping)
{
    if (header->messageType == MSG_LOG) {
        server_handle_log(datagram, header, payload_size, log_file);
    } else if (header->messageType == MSG_STOP) {
        if (payload_size != 0) {
            journal_error("ERROR STOP payloadSize=%u requestId=%u",
                          payload_size, header->requestId);
            return;
        }
        journal("RECEIVE requestId=%u type=STOP size=0 from_pid=%u",
                header->requestId, header->sourcePid);
        *stopping = true;
    } else if (header->messageType == MSG_BENCH) {
        server_handle_bench(header, payload_size);
    } else if (header->messageType == MSG_BENCH_END) {
        server_handle_bench_end(datagram, header, payload_size);
    }
}

int server_main(void)
{
    char path[108];
    char log_path[64];
    unsigned char datagram[MAX_DGRAM];
    FILE *log_file;
    int fd;
    int result = EXIT_SUCCESS;
    bool stopping = false;

    make_socket_path(path, sizeof(path));
    fd = open_server_socket(path);

    snprintf(log_path, sizeof(log_path), "received_%u.log", logging_run_id());
    log_file = fopen(log_path, "w");
    if (log_file == NULL) {
        close(fd);
        unlink(path);
        fatal_errno("fopen");
    }

    journal("START listening path=%s timeout=%ds log=%s",
            path, SERVER_TIMEOUT_SEC, log_path);

    while (!stopping) {
        MessageHeader header;
        uint32_t payload_size;
        ssize_t received;

        // MSG_TRUNC helps detect a datagram larger than the buffer
        received = recvfrom(fd, datagram, sizeof(datagram), MSG_TRUNC,
                            NULL, NULL);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                journal_error("ERROR func=recvfrom timeout=%ds",
                              SERVER_TIMEOUT_SEC);
                result = EXIT_FAILURE;
                break;
            }
            fatal_errno("recvfrom");
        }

        if ((size_t)received > sizeof(datagram)) {
            journal_error("ERROR datagram size=%zd exceeds buffer=%zu",
                          received, sizeof(datagram));
            continue;
        }
        if ((size_t)received < sizeof(header)) {
            journal_error("ERROR short message size=%zd", received);
            continue;
        }

        memcpy(&header, datagram, sizeof(header));
        payload_size = (uint32_t)received - (uint32_t)sizeof(header);

        if (header.version != MSG_VERSION) {
            journal_error("ERROR version=%u requestId=%u",
                          header.version, header.requestId);
            continue;
        }
        if (header.payloadSize != payload_size) {
            journal_error("ERROR size mismatch requestId=%u declared=%u actual=%u",
                          header.requestId, header.payloadSize, payload_size);
            continue;
        }
        if (message_type_name(header.messageType) == NULL) {
            journal_error("ERROR unknown type=%u requestId=%u size=%u",
                          header.messageType, header.requestId, payload_size);
            continue;
        }

        dispatch_message(datagram, &header, payload_size, log_file, &stopping);
    }

    close(fd);
    fclose(log_file);
    if (unlink(path) < 0 && errno != ENOENT) {
        journal_error("ERROR func=unlink errno=%d (%s)",
                      errno, strerror(errno));
    }
    journal("STOPPED result=%s",
            result == EXIT_SUCCESS ? "OK" : "FAIL");
    return result;
}
