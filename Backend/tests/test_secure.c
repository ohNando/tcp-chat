#include "test.h"
#include "secure.h"
#include "framer.h"

/* ---- In-memory transport: each side's emit appends to a queue ---- */

#define QMAX 16
typedef struct {
    uint8_t rec[QMAX][RECORD_MAX_BODY];
    size_t len[QMAX];
    int head, count;
} queue_t;

static queue_t to_server, to_client;

static int emit_q(void* ctx, const uint8_t* body, size_t len) {
    queue_t* q = ctx;
    if (q->count == QMAX || len > RECORD_MAX_BODY) return -1;
    int i = (q->head + q->count) % QMAX;
    memcpy(q->rec[i], body, len);
    q->len[i] = len;
    q->count++;
    return 0;
}

static int pop(queue_t* q, uint8_t** rec, size_t* len) {
    if (q->count == 0) return 0;
    *rec = q->rec[q->head];
    *len = q->len[q->head];
    q->head = (q->head + 1) % QMAX;
    q->count--;
    return 1;
}

static identity_t server_id;
static secure_t srv, cli;
static uint8_t out[RECORD_MAX_BODY];
static unsigned char pinned[HS_ID_PK_BYTES];
static int pin_enabled;

static int verify_pin(void* ctx, const unsigned char pk[HS_ID_PK_BYTES]) {
    (void)ctx;
    if (!pin_enabled) return 0;
    return memcmp(pk, pinned, HS_ID_PK_BYTES) == 0 ? 0 : -1;
}

/* Delivers queued records both ways until quiet. Returns -1 on error. */
static int pump(void) {
    uint8_t* rec = NULL; size_t len = 0, n = 0;
    int progress = 1;
    while (progress) {
        progress = 0;
        while (pop(&to_server, &rec, &len)) {
            progress = 1;
            if (secure_recv(&srv, rec, len, out, sizeof(out), &n) == SECURE_ERR) return -1;
        }
        while (pop(&to_client, &rec, &len)) {
            progress = 1;
            if (secure_recv(&cli, rec, len, out, sizeof(out), &n) == SECURE_ERR) return -1;
        }
    }
    return 0;
}

static void reset(void) {
    memset(&to_server, 0, sizeof(to_server));
    memset(&to_client, 0, sizeof(to_client));
    pin_enabled = 0;
    secure_server_init(&srv, &server_id, emit_q, &to_client);
    secure_client_init(&cli, verify_pin, emit_q, &to_server);
}

/* ---- Tests ---- */

TEST(handshake_establishes_both_sides) {
    reset();
    CHECK_EQ_INT(pump(), 0);
    CHECK(secure_established(&srv));
    CHECK(secure_established(&cli));
    CHECK_EQ_MEM(cli.peer_id_pk, server_id.pk, HS_ID_PK_BYTES);
}

TEST(data_flows_both_ways) {
    uint8_t* rec = NULL; size_t len = 0, n = 0;
    reset();
    pump();

    CHECK_EQ_INT(secure_send(&cli, (const uint8_t*)"hello server", 12, 0), 0);
    CHECK(pop(&to_server, &rec, &len));
    CHECK_EQ_INT(len, 12 + SECURE_OVERHEAD);
    CHECK(memcmp(rec, "hello server", 12) != 0); /* not plaintext on the wire */
    CHECK_EQ_INT(secure_recv(&srv, rec, len, out, sizeof(out), &n), SECURE_DATA);
    CHECK_EQ_INT(n, 12);
    CHECK_EQ_MEM(out, "hello server", 12);

    CHECK_EQ_INT(secure_send(&srv, (const uint8_t*)"hi client", 9, 0), 0);
    CHECK(pop(&to_client, &rec, &len));
    CHECK_EQ_INT(secure_recv(&cli, rec, len, out, sizeof(out), &n), SECURE_DATA);
    CHECK_EQ_MEM(out, "hi client", 9);
}

TEST(no_data_before_handshake) {
    reset();
    CHECK_EQ_INT(secure_send(&cli, (const uint8_t*)"x", 1, 0), -1);
}

TEST(tampered_record_is_rejected) {
    uint8_t* rec = NULL; size_t len = 0, n = 0;
    reset();
    pump();
    secure_send(&cli, (const uint8_t*)"transfer 10 TL", 14, 0);
    pop(&to_server, &rec, &len);
    rec[3] ^= 0x01;
    CHECK_EQ_INT(secure_recv(&srv, rec, len, out, sizeof(out), &n), SECURE_ERR);
    CHECK_EQ_INT(srv.state, SECURE_FAILED);
}

TEST(replayed_record_is_rejected) {
    uint8_t* rec = NULL; size_t len = 0, n = 0;
    uint8_t copy[RECORD_MAX_BODY];
    reset();
    pump();
    secure_send(&cli, (const uint8_t*)"once", 4, 0);
    pop(&to_server, &rec, &len);
    memcpy(copy, rec, len);

    CHECK_EQ_INT(secure_recv(&srv, rec, len, out, sizeof(out), &n), SECURE_DATA);
    CHECK_EQ_INT(secure_recv(&srv, copy, len, out, sizeof(out), &n), SECURE_ERR);
}

TEST(reordered_records_are_rejected) {
    uint8_t *a = NULL, *b = NULL; size_t alen = 0, blen = 0, n = 0;
    uint8_t first[RECORD_MAX_BODY];
    reset();
    pump();
    secure_send(&cli, (const uint8_t*)"first", 5, 0);
    secure_send(&cli, (const uint8_t*)"second", 6, 0);
    pop(&to_server, &a, &alen);
    memcpy(first, a, alen);
    pop(&to_server, &b, &blen);

    CHECK_EQ_INT(secure_recv(&srv, b, blen, out, sizeof(out), &n), SECURE_ERR);
}

TEST(final_record_ends_stream) {
    uint8_t* rec = NULL; size_t len = 0, n = 0;
    reset();
    pump();
    CHECK_EQ_INT(secure_send(&cli, (const uint8_t*)"bye", 3, 1), 0);
    CHECK_EQ_INT(secure_send(&cli, (const uint8_t*)"more", 4, 0), -1);

    pop(&to_server, &rec, &len);
    CHECK_EQ_INT(secure_recv(&srv, rec, len, out, sizeof(out), &n), SECURE_DATA);
    CHECK_EQ_INT(srv.state, SECURE_PEER_FINISHED);
}

TEST(invalid_signature_is_rejected) {
    uint8_t* rec = NULL; size_t len = 0, n = 0;
    reset();
    pop(&to_server, &rec, &len);
    secure_recv(&srv, rec, len, out, sizeof(out), &n);   /* server answers */

    pop(&to_client, &rec, &len);                         /* SERVER_HELLO */
    rec[len - 1] ^= 0x80;                                /* flip a signature bit */
    CHECK_EQ_INT(secure_recv(&cli, rec, len, out, sizeof(out), &n), SECURE_ERR);
    CHECK_EQ_STR(cli.error, "invalid server signature");
}

TEST(swapped_ephemeral_key_is_rejected) {
    uint8_t* rec = NULL; size_t len = 0, n = 0;
    reset();
    pop(&to_server, &rec, &len);
    secure_recv(&srv, rec, len, out, sizeof(out), &n);

    pop(&to_client, &rec, &len);
    /* A man in the middle replaces the server's ephemeral key with his own. */
    randombytes_buf(rec + HS_MAGIC_LEN + 1, HS_KX_PK_BYTES);
    CHECK_EQ_INT(secure_recv(&cli, rec, len, out, sizeof(out), &n), SECURE_ERR);
}

TEST(impostor_server_fails_pin_check) {
    identity_t impostor;
    unsigned char seed[crypto_sign_SEEDBYTES];
    randombytes_buf(seed, sizeof(seed));
    crypto_sign_seed_keypair(impostor.pk, impostor.sk, seed);

    reset();
    memcpy(pinned, server_id.pk, sizeof(pinned));
    pin_enabled = 1;
    /* Same protocol, valid signature, but a different identity key. */
    secure_server_init(&srv, &impostor, emit_q, &to_client);
    CHECK_EQ_INT(pump(), -1);
    CHECK_EQ_STR(cli.error, "server identity not trusted");

    reset();
    memcpy(pinned, server_id.pk, sizeof(pinned));
    pin_enabled = 1;
    CHECK_EQ_INT(pump(), 0);
    CHECK(secure_established(&cli));
}

TEST(garbage_hello_is_rejected) {
    size_t n;
    reset();
    CHECK_EQ_INT(secure_recv(&srv, (const uint8_t*)"GET / HTTP/1.1", 14, out, sizeof(out), &n),
                 SECURE_ERR);
}

int main(void) {
    if (sodium_init() < 0) return 1;
    unsigned char seed[crypto_sign_SEEDBYTES];
    randombytes_buf(seed, sizeof(seed));
    crypto_sign_seed_keypair(server_id.pk, server_id.sk, seed);

    RUN(handshake_establishes_both_sides);
    RUN(data_flows_both_ways);
    RUN(no_data_before_handshake);
    RUN(tampered_record_is_rejected);
    RUN(replayed_record_is_rejected);
    RUN(reordered_records_are_rejected);
    RUN(final_record_ends_stream);
    RUN(invalid_signature_is_rejected);
    RUN(swapped_ephemeral_key_is_rejected);
    RUN(impostor_server_fails_pin_check);
    RUN(garbage_hello_is_rejected);
    return TEST_REPORT();
}
