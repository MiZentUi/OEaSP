#ifndef SERVER_HANDLERS_H
#define SERVER_HANDLERS_H

#include <stdint.h>
#include <stdio.h>

#include "protocol.h"

void server_handle_log(const unsigned char *datagram,
                       const MessageHeader *header, uint32_t payload_size,
                       FILE *log_file);
void server_handle_bench(const MessageHeader *header, uint32_t payload_size);
void server_handle_bench_end(const unsigned char *datagram,
                             const MessageHeader *header,
                             uint32_t payload_size);

#endif
