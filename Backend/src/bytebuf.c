#include "bytebuf.h"

#include <string.h>

void bb_init_writer(bytebuf_t* b, uint8_t* data, size_t cap) {
    b->data = data;
    b->cap = cap;
    b->pos = 0;
    b->error = 0;
}

void bb_init_reader(bytebuf_t* b, const uint8_t* data, size_t len) {
    /* The reader never writes through `data`; the cast only lets both
     * modes share one struct. */
    b->data = (uint8_t*)data;
    b->cap = len;
    b->pos = 0;
    b->error = 0;
}

size_t bb_remaining(const bytebuf_t* b) {
    return b->cap - b->pos;
}

/* Reserves n bytes, returns a pointer to them or NULL on overflow. */
static uint8_t* take(bytebuf_t* b, size_t n) {
    if (b->error || n > b->cap - b->pos) {
        b->error = 1;
        return NULL;
    }
    uint8_t* p = b->data + b->pos;
    b->pos += n;
    return p;
}

int bb_put_u8(bytebuf_t* b, uint8_t v) {
    uint8_t* p = take(b, 1);
    if (!p) return -1;
    p[0] = v;
    return 0;
}

int bb_put_u16(bytebuf_t* b, uint16_t v) {
    uint8_t* p = take(b, 2);
    if (!p) return -1;
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    return 0;
}

int bb_put_u32(bytebuf_t* b, uint32_t v) {
    uint8_t* p = take(b, 4);
    if (!p) return -1;
    p[0] = (uint8_t)(v >> 24);
    p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);
    p[3] = (uint8_t)v;
    return 0;
}

int bb_put_u64(bytebuf_t* b, uint64_t v) {
    if (bb_put_u32(b, (uint32_t)(v >> 32)) < 0) return -1;
    return bb_put_u32(b, (uint32_t)v);
}

int bb_put_bytes(bytebuf_t* b, const void* src, size_t n) {
    uint8_t* p = take(b, n);
    if (!p) return -1;
    if (n) memcpy(p, src, n);
    return 0;
}

int bb_put_str(bytebuf_t* b, const char* s) {
    size_t n = strlen(s);
    if (n > UINT16_MAX) {
        b->error = 1;
        return -1;
    }
    if (bb_put_u16(b, (uint16_t)n) < 0) return -1;
    return bb_put_bytes(b, s, n);
}

int bb_get_u8(bytebuf_t* b, uint8_t* v) {
    uint8_t* p = take(b, 1);
    if (!p) return -1;
    *v = p[0];
    return 0;
}

int bb_get_u16(bytebuf_t* b, uint16_t* v) {
    uint8_t* p = take(b, 2);
    if (!p) return -1;
    *v = (uint16_t)((p[0] << 8) | p[1]);
    return 0;
}

int bb_get_u32(bytebuf_t* b, uint32_t* v) {
    uint8_t* p = take(b, 4);
    if (!p) return -1;
    *v = ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
         ((uint32_t)p[2] << 8) | (uint32_t)p[3];
    return 0;
}

int bb_get_u64(bytebuf_t* b, uint64_t* v) {
    uint32_t hi, lo;
    if (bb_get_u32(b, &hi) < 0 || bb_get_u32(b, &lo) < 0) return -1;
    *v = ((uint64_t)hi << 32) | lo;
    return 0;
}

int bb_get_bytes(bytebuf_t* b, void* dst, size_t n) {
    uint8_t* p = take(b, n);
    if (!p) return -1;
    if (n) memcpy(dst, p, n);
    return 0;
}

int bb_get_str(bytebuf_t* b, char* out, size_t out_cap) {
    uint16_t n;
    if (bb_get_u16(b, &n) < 0) return -1;
    if ((size_t)n + 1 > out_cap) {
        b->error = 1;
        return -1;
    }
    if (bb_get_bytes(b, out, n) < 0) return -1;
    out[n] = '\0';
    return 0;
}
