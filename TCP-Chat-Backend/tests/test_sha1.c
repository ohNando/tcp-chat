#include "test.h"
#include "sha1.h"

#include <stdio.h>

static void hex(const uint8_t d[SHA1_DIGEST_SIZE], char out[41]) {
    for (int i = 0; i < SHA1_DIGEST_SIZE; i++) sprintf(out + 2 * i, "%02x", d[i]);
}

static void check_vector(const char* msg, size_t len, const char* expected) {
    uint8_t d[SHA1_DIGEST_SIZE];
    char h[41];
    sha1(msg, len, d);
    hex(d, h);
    CHECK_EQ_STR(h, expected);
}

/* FIPS 180 / RFC 3174 test vectors. */
TEST(standard_vectors) {
    check_vector("", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    check_vector("abc", 3, "a9993e364706816aba3e25717850c26c9cd0d89d");
    const char* two_blocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    check_vector(two_blocks, strlen(two_blocks), "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
    check_vector("The quick brown fox jumps over the lazy dog", 43,
                 "2fd4e1c67a2d28fced849ee1bb76e7391b93eb12");
}

TEST(million_a) {
    sha1_ctx_t ctx;
    uint8_t d[SHA1_DIGEST_SIZE];
    char h[41], chunk[1000];
    memset(chunk, 'a', sizeof(chunk));

    sha1_init(&ctx);
    for (int i = 0; i < 1000; i++) sha1_update(&ctx, chunk, sizeof(chunk));
    sha1_final(&ctx, d);
    hex(d, h);
    CHECK_EQ_STR(h, "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

TEST(incremental_equals_one_shot) {
    const char* msg = "split across many odd sized updates to cross block borders!!";
    uint8_t a[SHA1_DIGEST_SIZE], b[SHA1_DIGEST_SIZE];
    sha1(msg, strlen(msg), a);

    sha1_ctx_t ctx;
    sha1_init(&ctx);
    for (size_t i = 0; i < strlen(msg); i += 7) {
        size_t n = strlen(msg) - i < 7 ? strlen(msg) - i : 7;
        sha1_update(&ctx, msg + i, n);
    }
    sha1_final(&ctx, b);
    CHECK_EQ_MEM(a, b, SHA1_DIGEST_SIZE);
}

int main(void) {
    RUN(standard_vectors);
    RUN(million_a);
    RUN(incremental_equals_one_shot);
    return TEST_REPORT();
}
