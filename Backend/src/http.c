#include "http.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

/* Finds "\r\n\r\n"; returns the offset just past it, or 0. */
static size_t find_end(const uint8_t* data, size_t len) {
    for (size_t i = 3; i < len; i++)
        if (data[i - 3] == '\r' && data[i - 2] == '\n' && data[i - 1] == '\r' && data[i] == '\n')
            return i + 1;
    return 0;
}

/* RFC 9110 token characters, used for methods and header names. */
static int is_tchar(char c) {
    return isalnum((unsigned char)c) || strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

static char* trim(char* s) {
    while (*s == ' ' || *s == '\t') s++;
    char* e = s + strlen(s);
    while (e > s && (e[-1] == ' ' || e[-1] == '\t')) *--e = '\0';
    return s;
}

static int parse_request_line(http_request_t* req, char* line) {
    char* sp1 = strchr(line, ' ');
    if (!sp1) return -1;
    char* sp2 = strchr(sp1 + 1, ' ');
    if (!sp2 || strchr(sp2 + 1, ' ')) return -1;
    *sp1 = *sp2 = '\0';

    for (char* p = line; *p; p++)
        if (!is_tchar(*p)) return -1;
    if (sp1 == line || sp1[1] != '/' ) return -1;   /* origin-form only */
    if (strcmp(sp2 + 1, "HTTP/1.1") != 0 && strcmp(sp2 + 1, "HTTP/1.0") != 0) return -1;

    req->method = line;
    req->target = sp1 + 1;
    req->version = sp2 + 1;
    return 0;
}

static int parse_header_line(http_request_t* req, char* line) {
    if (line[0] == ' ' || line[0] == '\t') return -1;   /* obsolete line folding */
    char* colon = strchr(line, ':');
    if (!colon || colon == line) return -1;
    *colon = '\0';
    for (char* p = line; *p; p++)
        if (!is_tchar(*p)) return -1;

    if (req->header_count == HTTP_MAX_HEADERS) return -1;
    req->headers[req->header_count].name = line;
    req->headers[req->header_count].value = trim(colon + 1);
    req->header_count++;
    return 0;
}

int http_parse_request(const uint8_t* data, size_t len, http_request_t* req) {
    size_t end = find_end(data, len);
    if (end == 0) return len >= HTTP_MAX_REQUEST ? -1 : 0;
    if (end > sizeof(req->buf)) return -1;

    memcpy(req->buf, data, end);
    req->buf[end - 2] = '\0'; /* cut the blank line */
    req->header_count = 0;

    for (size_t i = 0; i < end - 2; i++)
        if (req->buf[i] == '\0') return -1; /* embedded NUL */

    char* line = req->buf;
    int first = 1;
    while (*line) {
        char* eol = strstr(line, "\r\n");
        if (!eol) return -1;
        *eol = '\0';
        if (first) {
            if (parse_request_line(req, line) < 0) return -1;
            first = 0;
        } else if (parse_header_line(req, line) < 0) {
            return -1;
        }
        line = eol + 2;
    }
    return first ? -1 : (int)end;
}

const char* http_header(const http_request_t* req, const char* name) {
    for (int i = 0; i < req->header_count; i++)
        if (strcasecmp(req->headers[i].name, name) == 0) return req->headers[i].value;
    return NULL;
}

int http_header_has_token(const http_request_t* req, const char* name, const char* token) {
    size_t tlen = strlen(token);
    for (int i = 0; i < req->header_count; i++) {
        if (strcasecmp(req->headers[i].name, name) != 0) continue;

        const char* p = req->headers[i].value;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == ',') p++;
            const char* start = p;
            while (*p && *p != ',') p++;
            const char* end = p;
            while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
            if ((size_t)(end - start) == tlen && strncasecmp(start, token, tlen) == 0)
                return 1;
        }
    }
    return 0;
}
