#include "server_handlers.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#include "logging.h"

typedef struct {
    uint32_t received;
    bool started;
    struct timespec first;
    struct timespec last;
} BenchStat;

static BenchStat g_small;
static BenchStat g_large;

// records one benchmark message and its time
static void bench_note(BenchStat *stat)
{
    struct timespec now;

    clock_gettime(CLOCK_MONOTONIC, &now);
    if (!stat->started) {
        stat->first = now;
        stat->started = true;
    }
    stat->last = now;
    ++stat->received;
}

// prints benchmark results for one message size
static void bench_report(size_t message_size, const BenchStat *stat,
                         uint32_t sent, uint64_t client_ns)
{
    uint64_t server_ns;
    double server_seconds;
    double client_seconds;

    if (!stat->started) {
        journal_error("ERROR bench size=%zu received no messages", message_size);
        return;
    }

    server_ns = timespec_diff_ns(&stat->first, &stat->last);
    server_seconds = (double)server_ns / 1000000000.0;
    client_seconds = (double)client_ns / 1000000000.0;
    if (server_seconds <= 0.0) {
        server_seconds = 0.000000001;
    }
    if (client_seconds <= 0.0) {
        client_seconds = 0.000000001;
    }

    journal("BENCH RESULT size=%zu client_sent=%u server_received=%u "
            "server_elapsed_ms=%.3f server_rate=%.0f msg/s "
            "client_elapsed_ms=%.3f client_rate=%.0f msg/s",
            message_size, sent, stat->received,
            (double)server_ns / 1000000.0,
            (double)stat->received / server_seconds,
            (double)client_ns / 1000000.0,
            (double)sent / client_seconds);

    if (sent != stat->received) {
        journal_error("ERROR bench size=%zu lost messages: sent=%u received=%u",
                      message_size, sent, stat->received);
    }
}

void server_handle_log(const unsigned char *datagram,
                       const MessageHeader *header, uint32_t payload_size,
                       FILE *log_file)
{
    LogPayload payload;
    const size_t text_offset = offsetof(LogPayload, text);
    char text[LOG_TEXT_MAX + 1];
    char sent_text[32];

    if (payload_size < text_offset || payload_size > sizeof(LogPayload)) {
        journal_error("ERROR LOG payloadSize=%u requestId=%u",
                      payload_size, header->requestId);
        return;
    }

    memset(&payload, 0, sizeof(payload));
    memcpy(&payload, datagram + sizeof(*header), payload_size);

    if (payload.textLen > LOG_TEXT_MAX ||
        payload_size != text_offset + payload.textLen) {
        journal_error("ERROR LOG textLen=%u payloadSize=%u requestId=%u",
                      payload.textLen, payload_size, header->requestId);
        return;
    }

    memcpy(text, payload.text, payload.textLen);
    text[payload.textLen] = '\0';
    format_timestamp(payload.sentSec, payload.sentNsec,
                     sent_text, sizeof(sent_text));

    journal("RECEIVE requestId=%u type=LOG size=%u from_pid=%u "
            "seq=%u sent=%s text=\"%s\"",
            header->requestId, payload_size, header->sourcePid,
            payload.seq, sent_text, text);
    fprintf(log_file, "pid=%u seq=%u sent=%s text=\"%s\"\n",
            header->sourcePid, payload.seq, sent_text, text);
    fflush(log_file);
}

void server_handle_bench(const MessageHeader *header, uint32_t payload_size)
{
    size_t total_size = sizeof(MessageHeader) + (size_t)payload_size;

    if (total_size == BENCH_SMALL_SIZE) {
        bench_note(&g_small);
    } else if (total_size == BENCH_LARGE_SIZE) {
        bench_note(&g_large);
    } else {
        journal_error("ERROR BENCH size=%zu requestId=%u",
                      total_size, header->requestId);
    }
}

// validates the final datagram and reports both benchmark results
void server_handle_bench_end(const unsigned char *datagram,
                             const MessageHeader *header,
                             uint32_t payload_size)
{
    BenchEndPayload payload;

    if (payload_size != sizeof(payload)) {
        journal_error("ERROR BENCH_END payloadSize=%u requestId=%u",
                      payload_size, header->requestId);
        return;
    }
    memcpy(&payload, datagram + sizeof(*header), sizeof(payload));
    journal("RECEIVE requestId=%u type=BENCH_END size=%u from_pid=%u",
            header->requestId, payload_size, header->sourcePid);
    bench_report(BENCH_SMALL_SIZE, &g_small,
                 payload.smallSent, payload.smallNs);
    bench_report(BENCH_LARGE_SIZE, &g_large,
                 payload.largeSent, payload.largeNs);
}
