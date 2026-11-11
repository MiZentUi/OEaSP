#ifndef LR5_HTTP_H
#define LR5_HTTP_H

#include <stdbool.h>
#include <stddef.h>

#define HTTP_MAX_HEADER_BYTES 65536
#define HTTP_MAX_HEADERS 64
#define HTTP_MAX_NAME 64
#define HTTP_MAX_VALUE 512

typedef struct {
    char name[HTTP_MAX_NAME];
    char value[HTTP_MAX_VALUE];
} http_header_t;

typedef struct {
    char version[16];
    int status_code;
    char reason[128];
    http_header_t headers[HTTP_MAX_HEADERS];
    size_t header_count;
    size_t header_bytes;
    char *body;
    size_t body_len;
} http_response_t;

int http_build_request(const char *method, const char *path,
                       const char *host_header,
                       char *buffer, size_t capacity, size_t *size,
                       char *error, size_t error_size);
int http_find_head_end(const char *data, size_t size, size_t *head_size);
int http_parse_head(const char *data, size_t size, http_response_t *response,
                    char *error, size_t error_size);
const char *http_header_get(const http_response_t *response,
                            const char *name);
void http_response_free(http_response_t *response);
const char *http_status_text(int status_code);
void http_format_date(char *buffer, size_t size);

#endif
