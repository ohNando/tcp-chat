#include "test.h"
#include "handshake.h"

TEST(client_hello_round_trips) {
    client_hello_t in, out;
    uint8_t buf[64];
    in.version = HS_VERSION;
    randombytes_buf(in.eph_pk, sizeof(in.eph_pk));

    int n = client_hello_encode(&in, buf, sizeof(buf));
    CHECK_EQ_INT(n, CLIENT_HELLO_LEN);
    CHECK_EQ_MEM(buf, "TCHT", 4);
    CHECK_EQ_INT(client_hello_decode(buf, (size_t)n, &out), 0);
    CHECK_EQ_MEM(out.eph_pk, in.eph_pk, sizeof(in.eph_pk));
}

TEST(server_hello_round_trips) {
    server_hello_t in, out;
    uint8_t buf[256];
    in.version = HS_VERSION;
    randombytes_buf(in.eph_pk, sizeof(in.eph_pk));
    randombytes_buf(in.id_pk, sizeof(in.id_pk));
    randombytes_buf(in.sig, sizeof(in.sig));

    int n = server_hello_encode(&in, buf, sizeof(buf));
    CHECK_EQ_INT(n, SERVER_HELLO_LEN);
    CHECK_EQ_INT(server_hello_decode(buf, (size_t)n, &out), 0);
    CHECK_EQ_MEM(out.eph_pk, in.eph_pk, sizeof(in.eph_pk));
    CHECK_EQ_MEM(out.id_pk, in.id_pk, sizeof(in.id_pk));
    CHECK_EQ_MEM(out.sig, in.sig, sizeof(in.sig));
}

TEST(decode_rejects_bad_magic_version_and_length) {
    client_hello_t h = { .version = HS_VERSION };
    uint8_t buf[64];
    int n = client_hello_encode(&h, buf, sizeof(buf));

    CHECK_EQ_INT(client_hello_decode(buf, (size_t)n - 1, &h), -1);
    CHECK_EQ_INT(client_hello_decode(buf, (size_t)n + 1, &h), -1);

    buf[0] = 'X';
    CHECK_EQ_INT(client_hello_decode(buf, (size_t)n, &h), -1);
    buf[0] = 'T';
    buf[4] = HS_VERSION + 1;
    CHECK_EQ_INT(client_hello_decode(buf, (size_t)n, &h), -1);

    /* A client hello is not a server hello. */
    buf[4] = HS_VERSION;
    server_hello_t sh;
    CHECK_EQ_INT(server_hello_decode(buf, (size_t)n, &sh), -1);
}

TEST(transcript_layout) {
    unsigned char c[32], s[32], id[32];
    uint8_t t[HS_TRANSCRIPT_LEN];
    memset(c, 1, 32); memset(s, 2, 32); memset(id, 3, 32);

    handshake_transcript(t, c, s, id);
    CHECK_EQ_MEM(t, HS_CONTEXT, HS_CONTEXT_LEN);
    CHECK_EQ_MEM(t + HS_CONTEXT_LEN, c, 32);
    CHECK_EQ_MEM(t + HS_CONTEXT_LEN + 32, s, 32);
    CHECK_EQ_MEM(t + HS_CONTEXT_LEN + 64, id, 32);
}

int main(void) {
    if (sodium_init() < 0) return 1;
    RUN(client_hello_round_trips);
    RUN(server_hello_round_trips);
    RUN(decode_rejects_bad_magic_version_and_length);
    RUN(transcript_layout);
    return TEST_REPORT();
}
