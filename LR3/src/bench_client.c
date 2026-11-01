#include "bench_client.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "logging.h"
#include "protocol.h"
#include "transport.h"

int bench_client_main(void)
{
    static const size_t sizes[2] = { BENCH_SMALL_SIZE, BENCH_LARGE_SIZE };
    char path[108];
    struct sockaddr_un server_addr;
    unsigned char datagram[MAX_DGRAM];
    BenchEndPayload end_payload;
    uint32_t request_id = 1;
    int fd;

    make_socket_path(path, sizeof(path));
    fill_address(&server_addr, path);

    fd = socket(AF_UNIX, SOCK_DGRAM, 0);
    if (fd < 0) {
        fatal_errno("socket");
    }

    memset(&end_payload, 0, sizeof(end_payload));
    memset(datagram, 0x5a, sizeof(datagram));

    journal("START bench_count=%d sizes=%d,%d",
            BENCH_COUNT, BENCH_SMALL_SIZE, BENCH_LARGE_SIZE);

    for (size_t batch = 0; batch < 2; ++batch) {
        size_t message_size = sizes[batch];
        struct timespec start;
        struct timespec finish;
        uint64_t elapsed_ns;

        clock_gettime(CLOCK_MONOTONIC, &start);
        for (uint32_t i = 0; i < (uint32_t)BENCH_COUNT; ++i) {
            MessageHeader header;

            memset(&header, 0, sizeof(header));
            header.version = MSG_VERSION;
            header.messageType = MSG_BENCH;
            header.sourcePid = (uint32_t)getpid();
            header.requestId = request_id++;
            header.payloadSize = (uint32_t)(message_size - sizeof(header));
            memcpy(datagram, &header, sizeof(header));

            if (send_datagram(fd, &server_addr, datagram, message_size) < 0) {
                int error = errno;

                journal_error("ERROR func=sendto size=%zu errno=%d (%s)",
                              message_size, error, strerror(error));
                close(fd);
                return EXIT_FAILURE;
            }
        }
        clock_gettime(CLOCK_MONOTONIC, &finish);
        elapsed_ns = timespec_diff_ns(&start, &finish);

        if (batch == 0) {
            end_payload.smallSent = (uint32_t)BENCH_COUNT;
            end_payload.smallNs = elapsed_ns;
        } else {
            end_payload.largeSent = (uint32_t)BENCH_COUNT;
            end_payload.largeNs = elapsed_ns;
        }

        journal("BENCH SEND size=%zu count=%d bytes=%zu elapsed_ms=%.3f "
                "rate=%.0f msg/s",
                message_size, BENCH_COUNT, message_size * (size_t)BENCH_COUNT,
                (double)elapsed_ns / 1000000.0,
                (double)BENCH_COUNT /
                    ((double)elapsed_ns / 1000000000.0));
    }

    {
        MessageHeader header;

        memset(&header, 0, sizeof(header));
        header.version = MSG_VERSION;
        header.messageType = MSG_BENCH_END;
        header.sourcePid = (uint32_t)getpid();
        header.requestId = request_id++;
        header.payloadSize = (uint32_t)sizeof(end_payload);

        memcpy(datagram, &header, sizeof(header));
        memcpy(datagram + sizeof(header), &end_payload, sizeof(end_payload));

        if (send_datagram(fd, &server_addr, datagram,
                          sizeof(header) + sizeof(end_payload)) < 0) {
            int error = errno;

            journal_error("ERROR func=sendto type=BENCH_END errno=%d (%s)",
                          error, strerror(error));
            close(fd);
            return EXIT_FAILURE;
        }
        journal("SEND requestId=%u type=BENCH_END size=%zu",
                header.requestId, sizeof(header) + sizeof(end_payload));
    }

    journal("DONE bench");
    close(fd);
    return EXIT_SUCCESS;
}
