#include "server.h"

#include <errno.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "http.h"
#include "logging.h"
#include "net.h"
#include "timing.h"

#define SMALL_BODY_SIZE 128
#define MEDIUM_BODY_SIZE 8192
#define LARGE_BODY_SIZE (128 * 1024)

typedef struct {
    int status;
    const char *content_type;
    const char *body;
    size_t body_len;
    bool chunked;
    bool slow;
} route_t;

static volatile sig_atomic_t g_stop;
static unsigned long g_request_sequence;
static char g_small[SMALL_BODY_SIZE];
static char g_medium[MEDIUM_BODY_SIZE];
static char g_large[LARGE_BODY_SIZE];

static const char INDEX_BODY[] =
    "<!doctype html>\n"
    "<html><head><meta charset=\"utf-8\"><title>LR5 HTTP server</title></head>\n"
    "<body><h1>LR5 local HTTP test server</h1>\n"
    "<p>Variant 26: simplified HTTP/1.1 client.</p>\n"
    "<ul><li>/small</li><li>/medium</li><li>/large</li>"
    "<li>/empty</li><li>/chunked</li><li>/slow</li>"
    "<li>/missing</li></ul></body></html>\n";
static const char CHUNKED_BODY[] = "5\r\nhello\r\n0\r\n\r\n";
static const char SLOW_BODY[] = "slow response body\n";
static const char NOT_FOUND_BODY[] = "404 Not Found\n";
static const char BAD_REQUEST_BODY[] = "400 Bad Request\n";
static const char METHOD_BODY[] = "405 Method Not Allowed\n";
static const char TOO_LARGE_BODY[] = "431 Request Header Fields Too Large\n";
static const char TIMEOUT_BODY[] = "408 Request Timeout\n";

static void on_signal(int signal_number)
{
    (void)signal_number;
    g_stop = 1;
}

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

static void fill_pattern(char *buffer, size_t size)
{
    static const char alphabet[] =
        "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
    size_t index;

    for (index = 0; index < size; ++index) {
        buffer[index] = alphabet[index % (sizeof(alphabet) - 1)];
    }
}

static void route_for(const char *path, route_t *route)
{
    memset(route, 0, sizeof(*route));
    route->status = 200;
    route->content_type = "text/plain";

    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        route->content_type = "text/html";
        route->body = INDEX_BODY;
        route->body_len = sizeof(INDEX_BODY) - 1;
    } else if (strcmp(path, "/small") == 0) {
        route->body = g_small;
        route->body_len = sizeof(g_small);
    } else if (strcmp(path, "/medium") == 0) {
        route->body = g_medium;
        route->body_len = sizeof(g_medium);
    } else if (strcmp(path, "/large") == 0) {
        route->content_type = "application/octet-stream";
        route->body = g_large;
        route->body_len = sizeof(g_large);
    } else if (strcmp(path, "/empty") == 0) {
        route->body = "";
        route->body_len = 0;
    } else if (strcmp(path, "/chunked") == 0) {
        route->body = CHUNKED_BODY;
        route->body_len = sizeof(CHUNKED_BODY) - 1;
        route->chunked = true;
    } else if (strcmp(path, "/slow") == 0) {
        route->body = SLOW_BODY;
        route->body_len = sizeof(SLOW_BODY) - 1;
        route->slow = true;
    } else {
        route->status = 404;
        route->body = NOT_FOUND_BODY;
        route->body_len = sizeof(NOT_FOUND_BODY) - 1;
    }
}

static int send_response(int fd, const server_options_t *options,
                         int status, const char *content_type,
                         const char *body, size_t body_len,
                         bool chunked, bool send_body, bool slow,
                         bool allow_header, char *error, size_t error_size)
{
    char date[64];
    char framing[64];
    char header[1024];
    struct timespec deadline;
    int written;
    int result;

    http_format_date(date, sizeof(date));
    if (chunked) {
        snprintf(framing, sizeof(framing), "Transfer-Encoding: chunked\r\n");
    } else {
        snprintf(framing, sizeof(framing), "Content-Length: %zu\r\n",
                 body_len);
    }
    written = snprintf(header, sizeof(header),
                       "HTTP/1.1 %d %s\r\n"
                       "Date: %s\r\n"
                       "Server: lr5-http-server/1.0\r\n"
                       "Content-Type: %s\r\n"
                       "%s"
                       "Connection: close\r\n"
                       "%s"
                       "\r\n",
                       status, http_status_text(status), date, content_type,
                       framing,
                       allow_header ? "Allow: GET, HEAD\r\n" : "");
    if (written < 0 || (size_t)written >= sizeof(header)) {
        snprintf(error, error_size, "response header does not fit buffer");
        return -1;
    }

    timing_deadline_from_ms(options->timeout_ms, &deadline);
    result = net_write_all(fd, header, strlen(header), &deadline,
                           error, error_size);
    if (result < 0) {
        return result;
    }
    if (slow) {
        sleep_ms(options->slow_ms);
    }
    if (send_body && body_len > 0) {
        result = net_write_all(fd, body, body_len, &deadline,
                               error, error_size);
        if (result < 0) {
            return result;
        }
    }
    return 0;
}

static int parse_request(const char *buffer, size_t head_size,
                         char *method, size_t method_size,
                         char *path, size_t path_size,
                         char *version, size_t version_size,
                         bool *host_present)
{
    const char *line_end;
    const char *cursor;
    const char *head_end;
    char line[2048];
    size_t line_length;
    size_t index;

    line_end = memchr(buffer, '\r', head_size);
    if (line_end == NULL || line_end + 1 >= buffer + head_size ||
        line_end[1] != '\n') {
        return -1;
    }
    line_length = (size_t)(line_end - buffer);
    if (line_length == 0 || line_length >= sizeof(line)) {
        return -1;
    }
    memcpy(line, buffer, line_length);
    line[line_length] = '\0';
    if (sscanf(line, "%15s %2047s %15s", method, path, version) != 3) {
        return -1;
    }
    if (strlen(method) >= method_size || strlen(path) >= path_size ||
        strlen(version) >= version_size) {
        return -1;
    }
    if (strpbrk(path, "\r\n") != NULL || path[0] != '/') {
        return -1;
    }

    *host_present = false;
    head_end = buffer + head_size - 2;
    cursor = line_end + 2;
    while (cursor < head_end) {
        const char *next = NULL;
        const char *scan;

        for (scan = cursor; scan + 1 < buffer + head_size; ++scan) {
            if (scan[0] == '\r' && scan[1] == '\n') {
                next = scan;
                break;
            }
        }
        if (next == NULL || next > head_end) {
            return -1;
        }
        if (next == cursor) {
            break;
        }
        if (memchr(cursor, ':', (size_t)(next - cursor)) == NULL) {
            return -1;
        }
        for (index = 0; index < (size_t)(next - cursor); ++index) {
            if (cursor[index] == ':') {
                break;
            }
        }
        if ((size_t)(next - cursor) >= 5 &&
            strncasecmp(cursor, "Host:", 5) == 0) {
            *host_present = true;
        }
        cursor = next + 2;
    }
    return 0;
}

static void handle_client(int fd, const char *peer,
                          const server_options_t *options,
                          unsigned long request_id)
{
    char *buffer;
    size_t used = 0;
    size_t head_size = 0;
    struct timespec deadline;
    char error[256];
    char method[16] = "-";
    char path[2048] = "-";
    char version[16] = "-";
    bool host_present = false;

    buffer = malloc(options->max_request + 1);
    if (buffer == NULL) {
        log_error("SERVER", "peer=%s requestId=%lu op=ALLOC result=ERROR "
                  "error=\"malloc: %s\"",
                  peer, request_id, strerror(errno));
        net_close(fd);
        return;
    }
    timing_deadline_from_ms(options->timeout_ms, &deadline);

    while (!http_find_head_end(buffer, used, &head_size)) {
        int received;

        if (used >= options->max_request) {
            send_response(fd, options, 431, "text/plain", TOO_LARGE_BODY,
                          sizeof(TOO_LARGE_BODY) - 1, false, true, false,
                          false, error, sizeof(error));
            log_event("SERVER", "peer=%s requestId=%lu op=REQUEST method=%s "
                      "path=%s status=431 result=ERROR",
                      peer, request_id, method, path);
            goto done;
        }
        received = net_read_some(fd, buffer + used,
                                 options->max_request - used,
                                 &deadline, error, sizeof(error));
        if (received > 0) {
            used += (size_t)received;
            continue;
        }
        if (received == NET_IO_TIMEOUT) {
            send_response(fd, options, 408, "text/plain", TIMEOUT_BODY,
                          sizeof(TIMEOUT_BODY) - 1, false, true, false,
                          false, error, sizeof(error));
            log_event("SERVER", "peer=%s requestId=%lu op=REQUEST status=408 "
                      "result=ERROR",
                      peer, request_id);
            goto done;
        }
        if (received == NET_IO_EOF) {
            log_event("SERVER", "peer=%s requestId=%lu op=REQUEST status=- "
                      "result=ERROR error=\"client closed connection\"",
                      peer, request_id);
            goto done;
        }
        log_error("SERVER", "peer=%s requestId=%lu op=RECV result=ERROR "
                  "error=\"%s\"",
                  peer, request_id, error);
        goto done;
    }

    if (parse_request(buffer, head_size, method, sizeof(method),
                      path, sizeof(path), version, sizeof(version),
                      &host_present) < 0) {
        send_response(fd, options, 400, "text/plain", BAD_REQUEST_BODY,
                      sizeof(BAD_REQUEST_BODY) - 1, false, true, false,
                      false, error, sizeof(error));
        log_event("SERVER", "peer=%s requestId=%lu op=REQUEST status=400 "
                  "result=ERROR error=\"malformed request\"",
                  peer, request_id);
        goto done;
    }

    if (strcmp(version, "HTTP/1.1") != 0 &&
        strcmp(version, "HTTP/1.0") != 0) {
        send_response(fd, options, 400, "text/plain", BAD_REQUEST_BODY,
                      sizeof(BAD_REQUEST_BODY) - 1, false, true, false,
                      false, error, sizeof(error));
        log_event("SERVER", "peer=%s requestId=%lu op=REQUEST method=%s "
                  "path=%s status=400 result=ERROR error=\"bad version\"",
                  peer, request_id, method, path);
        goto done;
    }
    if (strcmp(version, "HTTP/1.1") == 0 && !host_present) {
        send_response(fd, options, 400, "text/plain", BAD_REQUEST_BODY,
                      sizeof(BAD_REQUEST_BODY) - 1, false, true, false,
                      false, error, sizeof(error));
        log_event("SERVER", "peer=%s requestId=%lu op=REQUEST method=%s "
                  "path=%s status=400 result=ERROR error=\"missing Host\"",
                  peer, request_id, method, path);
        goto done;
    }

    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        send_response(fd, options, 405, "text/plain", METHOD_BODY,
                      sizeof(METHOD_BODY) - 1, false, true, false,
                      true, error, sizeof(error));
        log_event("SERVER", "peer=%s requestId=%lu op=REQUEST method=%s "
                  "path=%s status=405 result=ERROR error=\"unknown method\"",
                  peer, request_id, method, path);
        goto done;
    }

    {
        route_t route;
        char *query = strchr(path, '?');
        int send_body = strcmp(method, "HEAD") != 0;
        int result;

        if (query != NULL) {
            *query = '\0';
        }
        route_for(path, &route);
        result = send_response(fd, options, route.status, route.content_type,
                               route.body, route.body_len, route.chunked,
                               send_body, route.slow, false,
                               error, sizeof(error));
        if (result < 0) {
            log_error("SERVER", "peer=%s requestId=%lu op=SEND method=%s "
                      "path=%s status=%d result=ERROR error=\"%s\"",
                      peer, request_id, method, path, route.status, error);
        } else {
            log_event("SERVER", "peer=%s requestId=%lu op=REQUEST method=%s "
                      "path=%s status=%d body=%zu result=OK",
                      peer, request_id, method, path, route.status,
                      (size_t)(send_body ? route.body_len : 0));
        }
    }

done:
    free(buffer);
    net_close(fd);
}

int server_run(const server_options_t *options)
{
    struct sigaction action;
    int listen_fd;
    char error[256];
    int exit_code = EXIT_SUCCESS;

    log_set_quiet(options->quiet);
    fill_pattern(g_small, sizeof(g_small));
    fill_pattern(g_medium, sizeof(g_medium));
    fill_pattern(g_large, sizeof(g_large));

    memset(&action, 0, sizeof(action));
    action.sa_handler = on_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) < 0 ||
        sigaction(SIGTERM, &action, NULL) < 0) {
        log_error("SERVER", "op=START result=ERROR error=\"sigaction: %s (%d)\"",
                  strerror(errno), errno);
        return EXIT_FAILURE;
    }
    signal(SIGPIPE, SIG_IGN);
    signal(SIGCHLD, SIG_IGN);

    listen_fd = net_listen(options->host, options->port, options->backlog,
                           error, sizeof(error));
    if (listen_fd < 0) {
        log_error("SERVER", "op=LISTEN host=%s port=%s result=ERROR "
                  "error=\"%s\"",
                  options->host, options->port, error);
        return EXIT_FAILURE;
    }
    log_event("SERVER", "op=START host=%s port=%s timeout_ms=%d backlog=%d "
              "max_request=%zu result=OK",
              options->host, options->port, options->timeout_ms,
              options->backlog, options->max_request);

    while (!g_stop) {
        char peer[NET_PEER_LEN];
        int client_fd;
        pid_t child;
        unsigned long request_id;

        if (net_accept(listen_fd, &client_fd, peer, sizeof(peer),
                       error, sizeof(error)) < 0) {
            if (g_stop) {
                break;
            }
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            log_error("SERVER", "op=ACCEPT result=ERROR error=\"%s\"",
                      error);
            exit_code = EXIT_FAILURE;
            break;
        }
        request_id = ++g_request_sequence;
        child = fork();
        if (child == 0) {
            signal(SIGINT, SIG_DFL);
            signal(SIGTERM, SIG_DFL);
            signal(SIGCHLD, SIG_DFL);
            net_close(listen_fd);
            handle_client(client_fd, peer, options, request_id);
            _exit(EXIT_SUCCESS);
        }
        if (child < 0) {
            log_error("SERVER", "peer=%s requestId=%lu op=FORK result=ERROR "
                      "error=\"fork: %s (%d)\"",
                      peer, request_id, strerror(errno), errno);
            net_close(client_fd);
            continue;
        }
        net_close(client_fd);
    }

    net_close(listen_fd);
    log_event("SERVER", "op=STOP result=OK");
    return exit_code;
}
