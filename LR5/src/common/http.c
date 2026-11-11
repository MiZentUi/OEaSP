#include "http.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

int http_find_head_end(const char *data, size_t size, size_t *head_size)
{
    size_t index;

    if (size < 4) {
        return 0;
    }
    for (index = 0; index + 3 < size; ++index) {
        if (data[index] == '\r' && data[index + 1] == '\n' &&
            data[index + 2] == '\r' && data[index + 3] == '\n') {
            *head_size = index + 4;
            return 1;
        }
    }
    return 0;
}

int http_build_request(const char *method, const char *path,
                       const char *host_header,
                       char *buffer, size_t capacity, size_t *size,
                       char *error, size_t error_size)
{
    int written;

    if (strcmp(method, "GET") != 0 && strcmp(method, "HEAD") != 0) {
        snprintf(error, error_size, "unsupported method: %s", method);
        return -1;
    }
    if (path == NULL || path[0] != '/') {
        snprintf(error, error_size, "path must start with /");
        return -1;
    }
    if (strpbrk(path, "\r\n") != NULL || host_header[0] == '\0' ||
        strpbrk(host_header, "\r\n") != NULL) {
        snprintf(error, error_size, "invalid request field");
        return -1;
    }

    written = snprintf(buffer, capacity,
                       "%s %s HTTP/1.1\r\n"
                       "Host: %s\r\n"
                       "User-Agent: lr5-http-client/1.0\r\n"
                       "Accept: */*\r\n"
                       "Connection: close\r\n"
                       "\r\n",
                       method, path, host_header);
    if (written < 0 || (size_t)written >= capacity) {
        snprintf(error, error_size, "request does not fit buffer");
        return -1;
    }
    *size = (size_t)written;
    return 0;
}

static int append_header(http_response_t *response,
                         const char *line, size_t length,
                         char *error, size_t error_size)
{
    const char *colon;
    const char *name_end;
    const char *value;
    const char *value_end;
    size_t name_length;
    size_t value_length;
    size_t index;

    if (response->header_count >= HTTP_MAX_HEADERS) {
        snprintf(error, error_size, "too many response headers");
        return -1;
    }
    colon = memchr(line, ':', length);
    if (colon == NULL) {
        snprintf(error, error_size, "malformed response header");
        return -1;
    }
    name_end = colon;
    while (name_end > line &&
           (name_end[-1] == ' ' || name_end[-1] == '\t')) {
        --name_end;
    }
    name_length = (size_t)(name_end - line);
    if (name_length == 0 || name_length >= HTTP_MAX_NAME) {
        snprintf(error, error_size, "invalid response header name");
        return -1;
    }
    for (index = 0; index < name_length; ++index) {
        unsigned char character = (unsigned char)line[index];

        if (!isalnum(character) && character != '-') {
            snprintf(error, error_size, "invalid response header name");
            return -1;
        }
    }

    value = colon + 1;
    value_end = line + length;
    while (value < value_end && (*value == ' ' || *value == '\t')) {
        ++value;
    }
    while (value_end > value &&
           (value_end[-1] == ' ' || value_end[-1] == '\t')) {
        --value_end;
    }
    value_length = (size_t)(value_end - value);
    if (value_length >= HTTP_MAX_VALUE) {
        snprintf(error, error_size, "response header value too long");
        return -1;
    }

    memcpy(response->headers[response->header_count].name,
           line, name_length);
    response->headers[response->header_count].name[name_length] = '\0';
    memcpy(response->headers[response->header_count].value,
           value, value_length);
    response->headers[response->header_count].value[value_length] = '\0';
    ++response->header_count;
    return 0;
}

int http_parse_head(const char *data, size_t size, http_response_t *response,
                    char *error, size_t error_size)
{
    char status_line[256];
    const char *line_end;
    const char *cursor;
    const char *head_end;
    char *space;
    char *code_text;
    char *after_code;
    long status_code;
    size_t line_length;
    size_t head_size;

    if (!http_find_head_end(data, size, &head_size)) {
        snprintf(error, error_size, "incomplete response header");
        return -1;
    }
    memset(response, 0, sizeof(*response));
    response->header_bytes = head_size;
    head_end = data + head_size - 2;

    line_end = memchr(data, '\r', head_size);
    if (line_end == NULL || line_end + 1 >= data + head_size ||
        line_end[1] != '\n') {
        snprintf(error, error_size, "malformed status line");
        return -1;
    }
    line_length = (size_t)(line_end - data);
    if (line_length == 0 || line_length >= sizeof(status_line)) {
        snprintf(error, error_size, "status line too long");
        return -1;
    }
    memcpy(status_line, data, line_length);
    status_line[line_length] = '\0';

    space = strchr(status_line, ' ');
    if (space == NULL) {
        snprintf(error, error_size, "malformed status line");
        return -1;
    }
    *space = '\0';
    if (strncmp(status_line, "HTTP/", 5) != 0 ||
        strlen(status_line) >= sizeof(response->version)) {
        snprintf(error, error_size, "unsupported HTTP version");
        return -1;
    }
    strcpy(response->version, status_line);

    code_text = space + 1;
    status_code = strtol(code_text, &after_code, 10);
    if (after_code == code_text || status_code < 100 ||
        status_code > 599 || (*after_code != '\0' && *after_code != ' ')) {
        snprintf(error, error_size, "invalid HTTP status code");
        return -1;
    }
    response->status_code = (int)status_code;
    if (*after_code == ' ') {
        const char *reason = after_code + 1;

        if (strlen(reason) >= sizeof(response->reason)) {
            snprintf(error, error_size, "status reason too long");
            return -1;
        }
        strcpy(response->reason, reason);
    }

    cursor = line_end + 2;
    while (cursor < head_end) {
        const char *next = NULL;
        const char *scan;

        for (scan = cursor; scan + 1 < data + size; ++scan) {
            if (scan[0] == '\r' && scan[1] == '\n') {
                next = scan;
                break;
            }
        }
        if (next == NULL || next > head_end) {
            snprintf(error, error_size, "malformed response header");
            return -1;
        }
        if (next == cursor) {
            break;
        }
        if (append_header(response, cursor, (size_t)(next - cursor),
                          error, error_size) < 0) {
            return -1;
        }
        cursor = next + 2;
    }
    return 0;
}

const char *http_header_get(const http_response_t *response,
                            const char *name)
{
    size_t index;

    for (index = 0; index < response->header_count; ++index) {
        if (strcasecmp(response->headers[index].name, name) == 0) {
            return response->headers[index].value;
        }
    }
    return NULL;
}

void http_response_free(http_response_t *response)
{
    free(response->body);
    response->body = NULL;
    response->body_len = 0;
}

const char *http_status_text(int status_code)
{
    switch (status_code) {
    case 200:
        return "OK";
    case 204:
        return "No Content";
    case 304:
        return "Not Modified";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    default:
        return "Unknown";
    }
}

void http_format_date(char *buffer, size_t size)
{
    time_t now = time(NULL);
    struct tm utc;

    if (size == 0) {
        return;
    }
    if (gmtime_r(&now, &utc) == NULL ||
        strftime(buffer, size, "%a, %d %b %Y %H:%M:%S GMT", &utc) == 0) {
        snprintf(buffer, size, "Thu, 01 Jan 1970 00:00:00 GMT");
    }
}
