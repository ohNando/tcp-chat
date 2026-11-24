#ifndef FRAMER_H
#define FRAMER_H

/*
 * TCP is a byte stream: one read() can return half a message, or three
 * messages glued together. The framer restores message boundaries.
 *
 * On the wire every record is:   u32 length (big-endian) | body[length]
 *
 * Typical use with a socket:
 *
 *   size_t avail;
 *   uint8_t* p = framer_space(&f, &avail);
 *   ssize_t n = read(fd, p, avail);
 *   framer_commit(&f, n);
 *   while ((r = framer_next(&f, &body, &len)) == 1) handle(body, len);
 *   if (r < 0) close_connection();
 */

#include <stddef.h>
#include <stdint.h>

#include "frame.h"

#define RECORD_HEADER_SIZE 4
/* Largest body we accept: a full frame plus room for encryption overhead. */
#define RECORD_MAX_BODY    (FRAME_MAX_SIZE + 64)

/* Buffers start small and grow on demand up to the size of one maximal
 * record, so idle connections cost 4 KiB instead of 64 KiB. */
#define FRAMER_INITIAL_CAP 4096

typedef struct {
    uint8_t* buf;
    size_t cap;      /* current allocation */
    size_t max_cap;  /* header + max_body: enough for any single record */
    size_t start;    /* first unconsumed byte */
    size_t end;      /* one past the last buffered byte */
    size_t max_body;
} framer_t;

int framer_init(framer_t* f, size_t max_body);
void framer_free(framer_t* f);

/* Free space to read into. Compacts the buffer first, grows it when it is
 * full, and shrinks it back once it is empty again. Returns *avail == 0
 * only when the buffer is full at its maximum size. */
uint8_t* framer_space(framer_t* f, size_t* avail);
void framer_commit(framer_t* f, size_t n);

/* Copies as much of `data` as fits (growing as needed); returns the number
 * of bytes taken. */
size_t framer_feed(framer_t* f, const void* data, size_t n);

/* 1: a record is available (body valid until the next space/feed call),
 * 0: need more bytes, -1: declared length exceeds max_body. */
int framer_next(framer_t* f, const uint8_t** body, size_t* len);

/* Raw access for protocols with their own framing (HTTP, WebSocket):
 * peek at all buffered bytes, then consume what was parsed. */
uint8_t* framer_peek(framer_t* f, size_t* len);
void framer_consume(framer_t* f, size_t n);

/* Bytes buffered but not yet returned as records. */
size_t framer_pending(const framer_t* f);

/* Writes the 4-byte length prefix for a record. */
void record_put_header(uint8_t out[RECORD_HEADER_SIZE], uint32_t body_len);

#endif
