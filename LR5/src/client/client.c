#include "client.h"

#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "http.h"
#include "logging.h"
#include "net.h"
#include "timing.h"

typedef struct {
    unsigned long requests;
    unsigned long successes;
    unsigned long errors;
    unsigned long timeouts;
    unsigned long status_2xx;
    unsigned long status_4xx;
    unsigned long status_5xx;
    uint64_t bytes_sent;
    uint64_t bytes_received;
    uint64_t connect_ns;
    uint64_t ttfb_ns;
    uint64_t total_ns;
} client_metrics_t;

static void sleep_ms(int milliseconds)
{
    struct timespec delay;

    if (milliseconds <= 0) {
        return;
    }
    delay.tv_sec = milliseconds / 1000;
    delay.tv_nsec = (long)(milliseconds % 1000) * 1000000L;
    while (nanosleep(&delay, &delay) < 0 && errno == EINTR) {
    }
}

static int parse_content_length(const char *text, size_t *value,
                                char *error, size_t error_size)
{
    char *end = NULL;
    unsigned long long parsed;

    if (text[0] == '\0' || !isdigit((unsigned char)text[0])) {
        snprintf(error, error_size, "invalid Content-Length: %s", text);
        return -1;
    }
    errno = 0;
    parsed = strtoull(text, &end, 10);
    if (errno != 0 || end == text || *end != '\0' ||
        parsed > (unsigned long long)SIZE_MAX) {
        snprintf(error, error_size, "invalid Content-Length: %s", text);
        return -1;
    }
    *value = (size_t)parsed;
    return 0;
}

static int read_body_until_close(int fd, const client_options_t *options,
                                 const char *header, size_t used,
                                 size_t head_size, http_response_t *response,
                                 const struct timespec *deadline,
                                 char *error, size_t error_size)
{
    size_t initial = used - head_size;
    size_t capacity = 4096;
    size_t length = initial;
    char *body;

    if (initial > options->max_body) {
        snprintf(error, error_size, "response body exceeds limit: %zu > %zu",
                 initial, options->max_body);
        return -1;
    }
    if (capacity > options->max_body) {
        capacity = options->max_body;
    }
    if (capacity < initial) {
        capacity = initial;
    }
    if (capacity == 0) {
        capacity = 1;
    }
    body = malloc(capacity);
    if (body == NULL) {
        snprintf(error, error_size, "malloc: %s", strerror(errno));
        return -1;
    }
    if (initial > 0) {
        memcpy(body, header + head_size, initial);
    }

    for (;;) {
        char chunk[4096];
        int received;

        received = net_read_some(fd, chunk, sizeof(chunk), deadline,
                                 error, error_size);
        if (received == NET_IO_EOF) {
            break;
        }
        if (received == NET_IO_TIMEOUT) {
            snprintf(error, error_size, "recv body: timeout");
            free(body);
            return NET_IO_TIMEOUT;
        }
        if (received < 0) {
            free(body);
            return -1;
        }
        if (length + (size_t)received > options->max_body) {
            snprintf(error, error_size,
                     "response body exceeds limit: %zu", options->max_body);
            free(body);
            return -1;
        }
        if (length + (size_t)received > capacity) {
            size_t new_capacity = capacity * 2;
            char *grown;

            while (new_capacity < length + (size_t)received) {
                new_capacity *= 2;
            }
            if (new_capacity > options->max_body) {
                new_capacity = options->max_body;
            }
            grown = realloc(body, new_capacity);
            if (grown == NULL) {
                snprintf(error, error_size, "realloc: %s", strerror(errno));
                free(body);
                return -1;
            }
            body = grown;
            capacity = new_capacity;
        }
        memcpy(body + length, chunk, (size_t)received);
        length += (size_t)received;
    }

    response->body = body;
    response->body_len = length;
    return 0;
}

static int read_response(int fd, const client_options_t *options,
                         http_response_t *response, size_t *wire_bytes,
                         uint64_t *first_byte_ns, uint64_t *header_done_ns,
                         char *error, size_t error_size)
{
    char *header;
    size_t used = 0;
    size_t head_size = 0;
    size_t content_length = 0;
    const char *transfer_encoding;
    const char *length_header;
    struct timespec deadline;
    int result = -1;

    header = malloc(HTTP_MAX_HEADER_BYTES + 1);
    if (header == NULL) {
        snprintf(error, error_size, "malloc: %s", strerror(errno));
        return -1;
    }
    timing_deadline_from_ms(options->timeout_ms, &deadline);

    while (!http_find_head_end(header, used, &head_size)) {
        int received;

        if (used >= HTTP_MAX_HEADER_BYTES) {
            snprintf(error, error_size, "response header exceeds %d bytes",
                     HTTP_MAX_HEADER_BYTES);
            goto cleanup;
        }
        received = net_read_some(fd, header + used,
                                 HTTP_MAX_HEADER_BYTES - used,
                                 &deadline, error, error_size);
        if (received > 0) {
            if (*first_byte_ns == 0) {
                *first_byte_ns = timing_now_ns();
            }
            used += (size_t)received;
            continue;
        }
        if (received == NET_IO_EOF) {
            snprintf(error, error_size,
                     "connection closed before response header");
            goto cleanup;
        }
        if (received == NET_IO_TIMEOUT) {
            snprintf(error, error_size, "recv header: timeout");
            result = NET_IO_TIMEOUT;
            goto cleanup;
        }
        goto cleanup;
    }

    if (http_parse_head(header, used, response, error, error_size) < 0) {
        goto cleanup;
    }
    *header_done_ns = timing_now_ns();

    transfer_encoding = http_header_get(response, "Transfer-Encoding");
    if (transfer_encoding != NULL &&
        strcasecmp(transfer_encoding, "identity") != 0) {
        snprintf(error, error_size, "unsupported transfer-encoding: %s",
                 transfer_encoding);
        goto cleanup;
    }

    if (strcmp(options->method, "HEAD") == 0 ||
        response->status_code == 204 || response->status_code == 304) {
        response->body = NULL;
        response->body_len = 0;
        *wire_bytes = head_size;
        result = 0;
        goto cleanup;
    }

    length_header = http_header_get(response, "Content-Length");
    if (length_header != NULL) {
        size_t available = used - head_size;
        size_t have = 0;

        if (parse_content_length(length_header, &content_length,
                                 error, error_size) < 0) {
            goto cleanup;
        }
        if (content_length > options->max_body) {
            snprintf(error, error_size,
                     "response body exceeds limit: %zu > %zu",
                     content_length, options->max_body);
            goto cleanup;
        }
        if (content_length > 0) {
            response->body = malloc(content_length);
            if (response->body == NULL) {
                snprintf(error, error_size, "malloc: %s", strerror(errno));
                goto cleanup;
            }
        }
        if (available > content_length) {
            available = content_length;
        }
        if (available > 0) {
            memcpy(response->body, header + head_size, available);
            have = available;
        }
        while (have < content_length) {
            int received = net_read_some(fd, response->body + have,
                                         content_length - have,
                                         &deadline, error, error_size);

            if (received > 0) {
                have += (size_t)received;
                continue;
            }
            if (received == NET_IO_EOF) {
                snprintf(error, error_size,
                         "connection closed before full body: %zu of %zu",
                         have, content_length);
                goto cleanup;
            }
            if (received == NET_IO_TIMEOUT) {
                snprintf(error, error_size, "recv body: timeout");
                result = NET_IO_TIMEOUT;
                goto cleanup;
            }
            goto cleanup;
        }
        response->body_len = content_length;
        *wire_bytes = head_size + content_length;
        result = 0;
        goto cleanup;
    }

    result = read_body_until_close(fd, options, header, used, head_size,
                                   response, &deadline, error, error_size);
    if (result == 0) {
        *wire_bytes = head_size + response->body_len;
    }

cleanup:
    free(header);
    if (result < 0) {
        http_response_free(response);
    }
    return result;
}

static int save_body(const char *path, const char *body, size_t length,
                     char *error, size_t error_size)
{
    FILE *stream;

    if (path[0] == '\0') {
        return 0;
    }
    if (strcmp(path, "-") == 0) {
        stream = stdout;
    } else {
        stream = fopen(path, "wb");
        if (stream == NULL) {
            snprintf(error, error_size, "fopen %s: %s (%d)",
                     path, strerror(errno), errno);
            return -1;
        }
    }
    if (length > 0 && fwrite(body, 1, length, stream) != length) {
        snprintf(error, error_size, "fwrite %s: %s (%d)",
                 path, strerror(errno), errno);
        if (stream != stdout) {
            fclose(stream);
        }
        return -1;
    }
    if (fflush(stream) != 0) {
        snprintf(error, error_size, "fflush %s: %s (%d)",
                 path, strerror(errno), errno);
        if (stream != stdout) {
            fclose(stream);
        }
        return -1;
    }
    if (stream != stdout && fclose(stream) != 0) {
        snprintf(error, error_size, "fclose %s: %s (%d)",
                 path, strerror(errno), errno);
        return -1;
    }
    return 0;
}

static void count_status(client_metrics_t *metrics, int status)
{
    if (status >= 200 && status < 300) {
        ++metrics->status_2xx;
    } else if (status >= 400 && status < 500) {
        ++metrics->status_4xx;
    } else if (status >= 500 && status < 600) {
        ++metrics->status_5xx;
    }
}

static void print_summary(const client_options_t *options,
                          const client_metrics_t *metrics, double total_ms)
{
    double average_ms = metrics->requests > 0
                      ? total_ms / (double)metrics->requests : 0.0;
    double average_connect_ms = metrics->successes > 0
        ? (double)metrics->connect_ns / 1000000.0 /
          (double)metrics->successes : 0.0;
    double average_ttfb_ms = metrics->successes > 0
        ? (double)metrics->ttfb_ns / 1000000.0 /
          (double)metrics->successes : 0.0;
    double bytes = (double)(metrics->bytes_sent + metrics->bytes_received);
    double throughput = total_ms > 0.0
        ? bytes / 1024.0 / (total_ms / 1000.0) : 0.0;

    printf("SUMMARY variant=26 id=%d host=%s port=%s method=%s path=%s "
           "requests=%lu success=%lu errors=%lu timeouts=%lu "
           "status_2xx=%lu status_4xx=%lu status_5xx=%lu "
           "sent_bytes=%llu recv_bytes=%llu total_ms=%.3f "
           "avg_request_ms=%.3f avg_connect_ms=%.3f avg_ttfb_ms=%.3f "
           "throughput_kib_s=%.3f\n",
           options->participant_id, options->host, options->port,
           options->method, options->path, metrics->requests,
           metrics->successes, metrics->errors, metrics->timeouts,
           metrics->status_2xx, metrics->status_4xx, metrics->status_5xx,
           (unsigned long long)metrics->bytes_sent,
           (unsigned long long)metrics->bytes_received,
           total_ms, average_ms, average_connect_ms, average_ttfb_ms,
           throughput);
    printf("RESULT: %s\n", metrics->errors == 0 ? "OK" : "FAILED");
}

int client_run(const client_options_t *options)
{
    client_metrics_t metrics;
    char host_header[CLIENT_HOST_MAX + CLIENT_PORT_MAX + 8];
    uint64_t run_start;
    unsigned long index;

    memset(&metrics, 0, sizeof(metrics));
    log_set_quiet(options->quiet);
    if (strchr(options->host, ':') != NULL) {
        snprintf(host_header, sizeof(host_header), "[%s]:%s",
                 options->host, options->port);
    } else {
        snprintf(host_header, sizeof(host_header), "%s:%s",
                 options->host, options->port);
    }

    run_start = timing_now_ns();
    for (index = 0; index < options->count; ++index) {
        unsigned long request_id = index + 1;
        char peer[NET_PEER_LEN];
        char error[256];
        char request[4096];
        size_t request_size = 0;
        size_t wire_bytes = 0;
        uint64_t total_start;
        uint64_t connect_start;
        uint64_t connect_done = 0;
        uint64_t send_done = 0;
        uint64_t first_byte_ns = 0;
        uint64_t header_done_ns = 0;
        struct timespec deadline;
        http_response_t response;
        int fd = -1;
        int result;

        snprintf(peer, sizeof(peer), "%s:%s", options->host, options->port);
        memset(&response, 0, sizeof(response));
        error[0] = '\0';
        total_start = timing_now_ns();
        ++metrics.requests;
        log_event("CLIENT",
                  "id=%d peer=%s op=REQUEST requestId=%lu method=%s path=%s",
                  options->participant_id, peer, request_id,
                  options->method, options->path);

        if (http_build_request(options->method, options->path, host_header,
                               request, sizeof(request), &request_size,
                               error, sizeof(error)) < 0) {
            ++metrics.errors;
            log_error("CLIENT",
                      "id=%d peer=%s op=BUILD requestId=%lu result=ERROR "
                      "error=\"%s\"",
                      options->participant_id, peer, request_id, error);
            metrics.total_ns += timing_now_ns() - total_start;
            sleep_ms(options->interval_ms);
            continue;
        }

        connect_start = timing_now_ns();
        result = net_connect(options->host, options->port, options->timeout_ms,
                             &fd, peer, sizeof(peer), error, sizeof(error));
        if (result < 0) {
            ++metrics.errors;
            if (result == NET_IO_TIMEOUT) {
                ++metrics.timeouts;
            }
            log_error("CLIENT",
                      "id=%d peer=%s op=CONNECT requestId=%lu result=ERROR "
                      "error=\"%s\"",
                      options->participant_id, peer, request_id, error);
            metrics.total_ns += timing_now_ns() - total_start;
            sleep_ms(options->interval_ms);
            continue;
        }
        connect_done = timing_now_ns();
        metrics.connect_ns += connect_done - connect_start;
        log_event("CLIENT",
                  "id=%d peer=%s op=CONNECT requestId=%lu result=OK "
                  "connect_ms=%.3f",
                  options->participant_id, peer, request_id,
                  timing_diff_ms(connect_start, connect_done));

        timing_deadline_from_ms(options->timeout_ms, &deadline);
        result = net_write_all(fd, request, request_size, &deadline,
                               error, sizeof(error));
        if (result < 0) {
            ++metrics.errors;
            if (result == NET_IO_TIMEOUT) {
                ++metrics.timeouts;
            }
            log_error("CLIENT",
                      "id=%d peer=%s op=SEND requestId=%lu result=ERROR "
                      "error=\"%s\"",
                      options->participant_id, peer, request_id, error);
            net_close(fd);
            metrics.total_ns += timing_now_ns() - total_start;
            sleep_ms(options->interval_ms);
            continue;
        }
        send_done = timing_now_ns();
        metrics.bytes_sent += request_size;
        log_event("CLIENT",
                  "id=%d peer=%s op=SEND requestId=%lu result=OK size=%zu",
                  options->participant_id, peer, request_id, request_size);

        result = read_response(fd, options, &response, &wire_bytes,
                               &first_byte_ns, &header_done_ns,
                               error, sizeof(error));
        if (result < 0) {
            ++metrics.errors;
            if (result == NET_IO_TIMEOUT) {
                ++metrics.timeouts;
            }
            log_error("CLIENT",
                      "id=%d peer=%s op=RECV requestId=%lu result=ERROR "
                      "error=\"%s\"",
                      options->participant_id, peer, request_id, error);
            net_close(fd);
            metrics.total_ns += timing_now_ns() - total_start;
            sleep_ms(options->interval_ms);
            continue;
        }

        ++metrics.successes;
        metrics.bytes_received += wire_bytes;
        if (first_byte_ns != 0) {
            metrics.ttfb_ns += first_byte_ns - send_done;
        }
        count_status(&metrics, response.status_code);
        log_event("CLIENT",
                  "id=%d peer=%s op=RECV requestId=%lu status=%d "
                  "reason=\"%s\" headers=%zu body=%zu wire=%zu "
                  "ttfb_ms=%.3f header_ms=%.3f total_ms=%.3f result=OK",
                  options->participant_id, peer, request_id,
                  response.status_code, response.reason,
                  response.header_count, response.body_len, wire_bytes,
                  first_byte_ns != 0
                      ? timing_diff_ms(send_done, first_byte_ns) : 0.0,
                  header_done_ns != 0
                      ? timing_diff_ms(send_done, header_done_ns) : 0.0,
                  timing_diff_ms(total_start, timing_now_ns()));

        if (save_body(options->output, response.body, response.body_len,
                      error, sizeof(error)) < 0) {
            ++metrics.errors;
            log_error("CLIENT",
                      "id=%d peer=%s op=SAVE requestId=%lu result=ERROR "
                      "error=\"%s\"",
                      options->participant_id, peer, request_id, error);
        }
        metrics.total_ns += timing_now_ns() - total_start;
        http_response_free(&response);
        net_close(fd);
        sleep_ms(options->interval_ms);
    }

    print_summary(options, &metrics,
                  timing_diff_ms(run_start, timing_now_ns()));
    return metrics.errors == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
