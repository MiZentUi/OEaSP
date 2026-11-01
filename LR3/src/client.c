#include "client.h"

#include <errno.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "logging.h"
#include "protocol.h"
#include "transport.h"

// builds the message text and pads it with dots to the required length
static void make_log_text(char *out, size_t size, uint32_t client_index,
                          uint32_t seq)
{
    int written = snprintf(out, size, "client=%u seq=%u variant=%d",
                           client_index, seq, VARIANT_ID);
    size_t length;
    size_t wanted = (size_t)LOG_TEXT_LEN;

    if (written < 0 || (size_t)written >= size) {
        journal_error("ERROR cannot format message text");
        exit(EXIT_FAILURE);
    }

    length = (size_t)written;
    while (length < wanted && length + 1 < size) {
        out[length++] = '.';
    }
    out[length] = '\0';
}

int client_main(uint32_t client_index)
{
    char path[108];
    struct sockaddr_un server_addr;
    unsigned char datagram[MAX_DGRAM];
    int fd;

    make_socket_path(path, sizeof(path));
    fill_address(&server_addr, path);

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        fatal_errno("socket");
    }

    journal("START client_index=%u messages=%d text_len=%d",
            client_index, CLIENT_MESSAGES, LOG_TEXT_LEN);

    for (uint32_t seq = 1; seq <= (uint32_t)CLIENT_MESSAGES; ++seq) {
        MessageHeader header;
        LogPayload payload;
        struct timespec sent;
        char text[LOG_TEXT_MAX + 1];
        uint32_t request_id = client_index * 1000U + seq;
        size_t payload_size = offsetof(LogPayload, text) + (size_t)LOG_TEXT_LEN;
        size_t datagram_size = sizeof(header) + payload_size;

        clock_gettime(CLOCK_REALTIME, &sent);
        make_log_text(text, sizeof(text), client_index, seq);

        memset(&payload, 0, sizeof(payload));
        payload.sentSec = sent.tv_sec;
        payload.sentNsec = sent.tv_nsec;
        payload.seq = seq;
        payload.textLen = (uint32_t)strlen(text);
        memcpy(payload.text, text, payload.textLen);

        memset(&header, 0, sizeof(header));
        header.version = MSG_VERSION;
        header.messageType = MSG_LOG;
        header.sourcePid = (uint32_t)getpid();
        header.requestId = request_id;
        header.payloadSize = (uint32_t)payload_size;

        memcpy(datagram, &header, sizeof(header));
        memcpy(datagram + sizeof(header), &payload, payload_size);

        if (send_datagram(fd, &server_addr, datagram, datagram_size) < 0) {
            int error = errno;

            journal_error("ERROR func=sendto requestId=%u errno=%d (%s)",
                          request_id, error, strerror(error));
            close(fd);
            return EXIT_FAILURE;
        }

        journal("SEND requestId=%u type=LOG size=%zu",
                request_id, datagram_size);
    }

    journal("DONE sent=%d", CLIENT_MESSAGES);
    close(fd);
    return EXIT_SUCCESS;
}
