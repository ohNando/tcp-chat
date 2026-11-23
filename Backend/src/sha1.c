#include "sha1.h"

#include <string.h>

static uint32_t rol(uint32_t x, int n) {
    return (x << n) | (x >> (32 - n));
}

/* Processes one 64-byte block. */
static void compress(uint32_t h[5], const uint8_t block[64]) {
    uint32_t w[80];

    for (int i = 0; i < 16; i++)
        w[i] = ((uint32_t)block[4 * i] << 24) | ((uint32_t)block[4 * i + 1] << 16) |
               ((uint32_t)block[4 * i + 2] << 8) | (uint32_t)block[4 * i + 3];
    for (int i = 16; i < 80; i++)
        w[i] = rol(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];

    for (int i = 0; i < 80; i++) {
        uint32_t f, k;
        if (i < 20)      { f = (b & c) | (~b & d);           k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d;                    k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d);  k = 0x8F1BBCDC; }
        else             { f = b ^ c ^ d;                    k = 0xCA62C1D6; }

        uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d;
        d = c;
        c = rol(b, 30);
        b = a;
        a = t;
    }

    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

void sha1_init(sha1_ctx_t* ctx) {
    ctx->h[0] = 0x67452301;
    ctx->h[1] = 0xEFCDAB89;
    ctx->h[2] = 0x98BADCFE;
    ctx->h[3] = 0x10325476;
    ctx->h[4] = 0xC3D2E1F0;
    ctx->total = 0;
    ctx->block_len = 0;
}

void sha1_update(sha1_ctx_t* ctx, const void* data, size_t len) {
    const uint8_t* p = data;
    ctx->total += len;

    while (len > 0) {
        size_t take = 64 - ctx->block_len;
        if (take > len) take = len;
        memcpy(ctx->block + ctx->block_len, p, take);
        ctx->block_len += take;
        p += take;
        len -= take;

        if (ctx->block_len == 64) {
            compress(ctx->h, ctx->block);
            ctx->block_len = 0;
        }
    }
}

void sha1_final(sha1_ctx_t* ctx, uint8_t out[SHA1_DIGEST_SIZE]) {
    uint64_t bits = ctx->total * 8;

    /* Padding: 0x80, zeros, then the message length in bits (64-bit BE),
     * so that the total is a multiple of 64 bytes. */
    uint8_t pad = 0x80;
    sha1_update(ctx, &pad, 1);
    uint8_t zero = 0;
    while (ctx->block_len != 56) sha1_update(ctx, &zero, 1);

    uint8_t len_be[8];
    for (int i = 0; i < 8; i++) len_be[i] = (uint8_t)(bits >> (56 - 8 * i));
    sha1_update(ctx, len_be, 8);

    for (int i = 0; i < 5; i++) {
        out[4 * i]     = (uint8_t)(ctx->h[i] >> 24);
        out[4 * i + 1] = (uint8_t)(ctx->h[i] >> 16);
        out[4 * i + 2] = (uint8_t)(ctx->h[i] >> 8);
        out[4 * i + 3] = (uint8_t)ctx->h[i];
    }
}

void sha1(const void* data, size_t len, uint8_t out[SHA1_DIGEST_SIZE]) {
    sha1_ctx_t ctx;
    sha1_init(&ctx);
    sha1_update(&ctx, data, len);
    sha1_final(&ctx, out);
}
