#ifndef BYTEBUF_H
#define BYTEBUF_H

/*
 * Bounds-checked big-endian (network byte order) reader/writer over a
 * caller-owned buffer.
 *
 * Errors are sticky: once a read or write would overflow, `error` is set
 * and every following call fails. This lets encoders/decoders chain many
 * calls and check the result once at the end.
 *
 * Strings on the wire are encoded as: u16 length + raw bytes (no NUL).
 */

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t* data;
    size_t cap;   /* writer: capacity, reader: total readable bytes */
    size_t pos;   /* writer: bytes written, reader: bytes consumed */
    int error;
} bytebuf_t;

void bb_init_writer(bytebuf_t* b, uint8_t* data, size_t cap);
void bb_init_reader(bytebuf_t* b, const uint8_t* data, size_t len);

size_t bb_remaining(const bytebuf_t* b);

int bb_put_u8(bytebuf_t* b, uint8_t v);
int bb_put_u16(bytebuf_t* b, uint16_t v);
int bb_put_u32(bytebuf_t* b, uint32_t v);
int bb_put_u64(bytebuf_t* b, uint64_t v);
int bb_put_bytes(bytebuf_t* b, const void* src, size_t n);
int bb_put_str(bytebuf_t* b, const char* s);

int bb_get_u8(bytebuf_t* b, uint8_t* v);
int bb_get_u16(bytebuf_t* b, uint16_t* v);
int bb_get_u32(bytebuf_t* b, uint32_t* v);
int bb_get_u64(bytebuf_t* b, uint64_t* v);
int bb_get_bytes(bytebuf_t* b, void* dst, size_t n);
/* Copies the string into `out` and NUL-terminates it.
 * Fails if it does not fit in `out_cap` (including the terminator). */
int bb_get_str(bytebuf_t* b, char* out, size_t out_cap);

#endif
