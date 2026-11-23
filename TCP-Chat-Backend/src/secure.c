#include "secure.h"
#include "framer.h"

#include <string.h>

static int fail(secure_t* s, const char* why) {
    s->state = SECURE_FAILED;
    s->error = why;
    sodium_memzero(s->eph_sk, sizeof(s->eph_sk));
    sodium_memzero(s->rx_key, sizeof(s->rx_key));
    return SECURE_ERR;
}

/* Starts our sending stream and emits its header. */
static int start_tx(secure_t* s, const unsigned char tx_key[SECURE_KEY_BYTES]) {
    unsigned char header[SECURE_HEADER_BYTES];
    crypto_secretstream_xchacha20poly1305_init_push(&s->tx, header, tx_key);
    return s->emit(s->ctx, header, sizeof(header));
}

void secure_server_init(secure_t* s, const identity_t* id,
                        secure_emit_fn emit, void* ctx) {
    memset(s, 0, sizeof(*s));
    s->role = SECURE_SERVER;
    s->state = SECURE_WAIT_HELLO;
    s->identity = id;
    s->emit = emit;
    s->ctx = ctx;
}

int secure_client_init(secure_t* s, secure_verify_fn verify,
                       secure_emit_fn emit, void* ctx) {
    memset(s, 0, sizeof(*s));
    s->role = SECURE_CLIENT;
    s->state = SECURE_WAIT_HELLO;
    s->verify = verify;
    s->emit = emit;
    s->ctx = ctx;

    crypto_kx_keypair(s->eph_pk, s->eph_sk);

    client_hello_t hello = { .version = HS_VERSION };
    memcpy(hello.eph_pk, s->eph_pk, sizeof(hello.eph_pk));

    uint8_t buf[CLIENT_HELLO_LEN];
    int n = client_hello_encode(&hello, buf, sizeof(buf));
    if (n < 0 || emit(ctx, buf, (size_t)n) < 0) return fail(s, "cannot send client hello");
    return 0;
}

/* Server: CLIENT_HELLO -> SERVER_HELLO + header. */
static int server_on_hello(secure_t* s, const uint8_t* rec, size_t len) {
    client_hello_t ch;
    if (client_hello_decode(rec, len, &ch) < 0) return fail(s, "bad client hello");

    crypto_kx_keypair(s->eph_pk, s->eph_sk);

    unsigned char tx_key[SECURE_KEY_BYTES];
    /* Fails for degenerate (low-order) peer keys. */
    if (crypto_kx_server_session_keys(s->rx_key, tx_key, s->eph_pk, s->eph_sk, ch.eph_pk) != 0)
        return fail(s, "key exchange failed");
    sodium_memzero(s->eph_sk, sizeof(s->eph_sk));

    server_hello_t sh = { .version = HS_VERSION };
    memcpy(sh.eph_pk, s->eph_pk, sizeof(sh.eph_pk));
    memcpy(sh.id_pk, s->identity->pk, sizeof(sh.id_pk));

    uint8_t transcript[HS_TRANSCRIPT_LEN];
    handshake_transcript(transcript, ch.eph_pk, sh.eph_pk, sh.id_pk);
    crypto_sign_detached(sh.sig, NULL, transcript, sizeof(transcript), s->identity->sk);

    uint8_t buf[SERVER_HELLO_LEN];
    int n = server_hello_encode(&sh, buf, sizeof(buf));
    int rc = (n < 0 || s->emit(s->ctx, buf, (size_t)n) < 0) ? -1 : start_tx(s, tx_key);
    sodium_memzero(tx_key, sizeof(tx_key));
    if (rc < 0) return fail(s, "cannot send server hello");

    s->state = SECURE_WAIT_HEADER;
    return SECURE_NONE;
}

/* Client: SERVER_HELLO -> verify, derive keys, send header. */
static int client_on_hello(secure_t* s, const uint8_t* rec, size_t len) {
    server_hello_t sh;
    if (server_hello_decode(rec, len, &sh) < 0) return fail(s, "bad server hello");

    uint8_t transcript[HS_TRANSCRIPT_LEN];
    handshake_transcript(transcript, s->eph_pk, sh.eph_pk, sh.id_pk);
    if (crypto_sign_verify_detached(sh.sig, transcript, sizeof(transcript), sh.id_pk) != 0)
        return fail(s, "invalid server signature");

    memcpy(s->peer_id_pk, sh.id_pk, sizeof(s->peer_id_pk));
    if (s->verify && s->verify(s->ctx, sh.id_pk) != 0)
        return fail(s, "server identity not trusted");

    unsigned char tx_key[SECURE_KEY_BYTES];
    if (crypto_kx_client_session_keys(s->rx_key, tx_key, s->eph_pk, s->eph_sk, sh.eph_pk) != 0)
        return fail(s, "key exchange failed");
    sodium_memzero(s->eph_sk, sizeof(s->eph_sk));

    int rc = start_tx(s, tx_key);
    sodium_memzero(tx_key, sizeof(tx_key));
    if (rc < 0) return fail(s, "cannot send stream header");

    s->state = SECURE_WAIT_HEADER;
    return SECURE_NONE;
}

static int on_header(secure_t* s, const uint8_t* rec, size_t len) {
    if (len != SECURE_HEADER_BYTES) return fail(s, "bad stream header");
    if (crypto_secretstream_xchacha20poly1305_init_pull(&s->rx, rec, s->rx_key) != 0)
        return fail(s, "bad stream header");
    sodium_memzero(s->rx_key, sizeof(s->rx_key));
    s->state = SECURE_ESTABLISHED;
    return SECURE_NONE;
}

static int on_data(secure_t* s, const uint8_t* rec, size_t len,
                   uint8_t* out, size_t cap, size_t* out_len) {
    if (len < SECURE_OVERHEAD || cap < len - SECURE_OVERHEAD)
        return fail(s, "record too short or output too small");

    unsigned long long mlen;
    unsigned char tag;
    if (crypto_secretstream_xchacha20poly1305_pull(&s->rx, out, &mlen, &tag,
                                                   rec, len, NULL, 0) != 0)
        return fail(s, "decryption failed (tampered, replayed or reordered record)");

    if (tag == crypto_secretstream_xchacha20poly1305_TAG_FINAL)
        s->state = SECURE_PEER_FINISHED;
    *out_len = (size_t)mlen;
    return SECURE_DATA;
}

int secure_recv(secure_t* s, const uint8_t* rec, size_t len,
                uint8_t* out, size_t cap, size_t* out_len) {
    *out_len = 0;
    switch (s->state) {
        case SECURE_WAIT_HELLO:
            return s->role == SECURE_SERVER ? server_on_hello(s, rec, len)
                                            : client_on_hello(s, rec, len);
        case SECURE_WAIT_HEADER:
            return on_header(s, rec, len);
        case SECURE_ESTABLISHED:
            return on_data(s, rec, len, out, cap, out_len);
        case SECURE_PEER_FINISHED:
            return fail(s, "record after end of stream");
        default:
            return SECURE_ERR;
    }
}

int secure_send(secure_t* s, const uint8_t* plain, size_t len, int final) {
    static uint8_t buf[RECORD_MAX_BODY];

    if (s->tx_finished) return -1;
    if (s->state != SECURE_ESTABLISHED && s->state != SECURE_PEER_FINISHED) return -1;
    if (len + SECURE_OVERHEAD > sizeof(buf)) return -1;

    unsigned long long clen;
    unsigned char tag = final ? crypto_secretstream_xchacha20poly1305_TAG_FINAL
                              : crypto_secretstream_xchacha20poly1305_TAG_MESSAGE;
    crypto_secretstream_xchacha20poly1305_push(&s->tx, buf, &clen, plain, len, NULL, 0, tag);
    if (final) s->tx_finished = 1;
    return s->emit(s->ctx, buf, (size_t)clen);
}

int secure_established(const secure_t* s) {
    return s->state == SECURE_ESTABLISHED;
}

void secure_wipe(secure_t* s) {
    sodium_memzero(s, sizeof(*s));
}
