#ifndef TEST_PEER_H
#define TEST_PEER_H

/*
 * Test helper: completes the secure handshake for a server-side conn_t
 * without any sockets. A client secure_t (`test_peer`) feeds its records
 * straight into the connection, and the connection's queued output is fed
 * back to the client.
 */

#include "conn.h"
#include "framer.h"

static identity_t test_identity;
static secure_t test_peer;
static uint8_t test_plain[RECORD_MAX_BODY];

static void test_identity_init(void) {
    unsigned char seed[crypto_sign_SEEDBYTES];
    randombytes_buf(seed, sizeof(seed));
    crypto_sign_seed_keypair(test_identity.pk, test_identity.sk, seed);
}

static int peer_to_server(void* ctx, const uint8_t* body, size_t len) {
    conn_t* c = ctx;
    size_t n;
    return secure_recv(&c->sec, body, len, test_plain, sizeof(test_plain), &n) == SECURE_ERR
               ? -1 : 0;
}

/* Feeds every record queued in c->out to the test peer, then clears it.
 * Returns the number of SECURE_DATA records, or -1 on error. Plaintext of
 * the last one is left in test_plain. */
static int deliver_to_peer(conn_t* c, size_t* last_len) {
    framer_t f;
    const uint8_t* body;
    size_t len;
    int data = 0;

    framer_init(&f, RECORD_MAX_BODY);
    framer_feed(&f, c->out + c->out_off, conn_pending_output(c));
    c->out_off = c->out_len = 0;

    while (framer_next(&f, &body, &len) == 1) {
        int r = secure_recv(&test_peer, body, len, test_plain, sizeof(test_plain), last_len);
        if (r == SECURE_ERR) { data = -1; break; }
        if (r == SECURE_DATA) data++;
    }
    framer_free(&f);
    return data;
}

static int establish(conn_t* c) {
    size_t n;
    if (secure_client_init(&test_peer, NULL, peer_to_server, c) < 0) return -1;
    if (deliver_to_peer(c, &n) < 0) return -1;
    return secure_established(&c->sec) && secure_established(&test_peer) ? 0 : -1;
}

#endif
