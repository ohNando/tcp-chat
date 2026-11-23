#ifndef SHA1_H
#define SHA1_H

/*
 * SHA-1 (FIPS 180-4).
 *
 * Only used because RFC 6455 requires it to compute Sec-WebSocket-Accept.
 * SHA-1 is broken for collision resistance: never use it for security.
 */

#include <stddef.h>
#include <stdint.h>

#define SHA1_DIGEST_SIZE 20

typedef struct {
    uint32_t h[5];
    uint64_t total;       /* bytes hashed so far */
    uint8_t block[64];
    size_t block_len;
} sha1_ctx_t;

void sha1_init(sha1_ctx_t* ctx);
void sha1_update(sha1_ctx_t* ctx, const void* data, size_t len);
void sha1_final(sha1_ctx_t* ctx, uint8_t out[SHA1_DIGEST_SIZE]);

/* One-shot helper. */
void sha1(const void* data, size_t len, uint8_t out[SHA1_DIGEST_SIZE]);

#endif
