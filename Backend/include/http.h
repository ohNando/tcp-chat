#ifndef HTTP_H
#define HTTP_H

/*
 * Minimal HTTP/1.1 request parser: request line + headers, no body.
 * Enough for a WebSocket upgrade and serving static files with GET.
 *
 * The parsed request owns a copy of the header block; header names and
 * values point into it.
 */

#include <stddef.h>
#include <stdint.h>

#define HTTP_MAX_REQUEST 8192
#define HTTP_MAX_HEADERS 32

typedef struct {
    const char* name;
    const char* value;
} http_header_t;

typedef struct {
    char buf[HTTP_MAX_REQUEST];
    const char* method;
    const char* target;
    const char* version;
    http_header_t headers[HTTP_MAX_HEADERS];
    int header_count;
} http_request_t;

/* > 0: a complete request was parsed, returns its size in bytes.
 *   0: incomplete, read more.
 *  -1: malformed or larger than HTTP_MAX_REQUEST. */
int http_parse_request(const uint8_t* data, size_t len, http_request_t* req);

/* Header value (case-insensitive name), or NULL. */
const char* http_header(const http_request_t* req, const char* name);

/* True if a comma separated header contains `token` (case-insensitive),
 * e.g. "Connection: keep-alive, Upgrade" has "upgrade". */
int http_header_has_token(const http_request_t* req, const char* name, const char* token);

#endif
