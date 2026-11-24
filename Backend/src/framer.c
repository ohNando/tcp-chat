#include "framer.h"

#include <stdlib.h>
#include <string.h>

int framer_init(framer_t* f, size_t max_body) {
    /* One partial record never needs more than header + max_body bytes,
     * so after compaction and growth there is always room to finish it. */
    f->max_cap = RECORD_HEADER_SIZE + max_body;
    f->cap = f->max_cap < FRAMER_INITIAL_CAP ? f->max_cap : FRAMER_INITIAL_CAP;
    f->buf = malloc(f->cap);
    f->start = 0;
    f->end = 0;
    f->max_body = max_body;
    return f->buf ? 0 : -1;
}

void framer_free(framer_t* f) {
    free(f->buf);
    f->buf = NULL;
    f->cap = f->max_cap = f->start = f->end = 0;
}

static void compact(framer_t* f) {
    if (f->start == 0) return;
    size_t pending = f->end - f->start;
    if (pending) memmove(f->buf, f->buf + f->start, pending);
    f->start = 0;
    f->end = pending;
}

static void resize(framer_t* f, size_t cap) {
    uint8_t* p = realloc(f->buf, cap);
    if (!p) return; /* keep the old buffer; the caller just sees less space */
    f->buf = p;
    f->cap = cap;
}

uint8_t* framer_space(framer_t* f, size_t* avail) {
    compact(f);

    size_t initial = f->max_cap < FRAMER_INITIAL_CAP ? f->max_cap : FRAMER_INITIAL_CAP;
    if (f->end == 0 && f->cap > initial) {
        resize(f, initial);                /* burst is over: give memory back */
    } else if (f->end == f->cap && f->cap < f->max_cap) {
        size_t cap = f->cap * 2;           /* full: grow geometrically */
        resize(f, cap < f->max_cap ? cap : f->max_cap);
    }

    *avail = f->cap - f->end;
    return f->buf + f->end;
}

void framer_commit(framer_t* f, size_t n) {
    if (n > f->cap - f->end) n = f->cap - f->end;
    f->end += n;
}

size_t framer_feed(framer_t* f, const void* data, size_t n) {
    const uint8_t* src = data;
    size_t taken = 0;
    while (taken < n) {
        size_t avail;
        uint8_t* p = framer_space(f, &avail);
        if (avail == 0) break;
        size_t k = n - taken < avail ? n - taken : avail;
        memcpy(p, src + taken, k);
        framer_commit(f, k);
        taken += k;
    }
    return taken;
}

int framer_next(framer_t* f, const uint8_t** body, size_t* len) {
    size_t pending = f->end - f->start;
    if (pending < RECORD_HEADER_SIZE) return 0;

    const uint8_t* p = f->buf + f->start;
    uint32_t body_len = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
                        ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    if (body_len > f->max_body) return -1;
    if (pending - RECORD_HEADER_SIZE < body_len) return 0;

    *body = p + RECORD_HEADER_SIZE;
    *len = body_len;
    f->start += RECORD_HEADER_SIZE + body_len;
    return 1;
}

uint8_t* framer_peek(framer_t* f, size_t* len) {
    *len = f->end - f->start;
    return f->buf + f->start;
}

void framer_consume(framer_t* f, size_t n) {
    size_t pending = f->end - f->start;
    f->start += n > pending ? pending : n;
}

size_t framer_pending(const framer_t* f) {
    return f->end - f->start;
}

void record_put_header(uint8_t out[RECORD_HEADER_SIZE], uint32_t body_len) {
    out[0] = (uint8_t)(body_len >> 24);
    out[1] = (uint8_t)(body_len >> 16);
    out[2] = (uint8_t)(body_len >> 8);
    out[3] = (uint8_t)body_len;
}
