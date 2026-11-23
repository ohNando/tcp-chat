#ifndef SECURE_H
#define SECURE_H

/*
 * Secure channel: handshake + encrypted record layer, independent of any
 * socket. It consumes received record bodies and produces record bodies to
 * send through the `emit` callback, so the same code runs over TCP, over
 * WebSocket, and inside unit tests.
 *
 * Handshake
 *   C -> S  CLIENT_HELLO (ephemeral X25519 key)
 *   S -> C  SERVER_HELLO (ephemeral key, identity key, Ed25519 signature)
 *   S -> C  server secretstream header (24 bytes)
 *   C -> S  client secretstream header (24 bytes)
 *
 *   Both sides run crypto_kx on the ephemeral keys to get one key per
 *   direction (forward secrecy: the ephemeral secrets are wiped right
 *   away). The client checks the signature, then asks `verify` whether it
 *   trusts the server's identity key (pinning).
 *
 * Records
 *   Each record body is crypto_secretstream_xchacha20poly1305 output:
 *   XChaCha20 encryption + Poly1305 tag (17 bytes overhead). The stream
 *   keeps an internal counter, so a tampered, replayed, dropped or
 *   reordered record fails to decrypt. TAG_FINAL marks the last record.
 */

#include <stddef.h>
#include <stdint.h>
#include <sodium.h>

#include "handshake.h"
#include "identity.h"

#define SECURE_HEADER_BYTES crypto_secretstream_xchacha20poly1305_HEADERBYTES /* 24 */
#define SECURE_OVERHEAD     crypto_secretstream_xchacha20poly1305_ABYTES      /* 17 */
#define SECURE_KEY_BYTES    crypto_kx_SESSIONKEYBYTES                         /* 32 */

typedef enum {
    SECURE_CLIENT,
    SECURE_SERVER,
} secure_role_t;

typedef enum {
    SECURE_WAIT_HELLO,     /* server: CLIENT_HELLO, client: SERVER_HELLO */
    SECURE_WAIT_HEADER,    /* peer's secretstream header */
    SECURE_ESTABLISHED,
    SECURE_PEER_FINISHED,  /* peer sent TAG_FINAL */
    SECURE_FAILED,
} secure_state_t;

/* secure_recv() results */
#define SECURE_ERR   (-1)
#define SECURE_NONE    0   /* handshake progressed, nothing for the app */
#define SECURE_DATA    1   /* plaintext written to out */

/* Sends one record body. Returns 0 or -1. */
typedef int (*secure_emit_fn)(void* ctx, const uint8_t* body, size_t len);
/* Client only: return 0 to trust the server identity key, -1 to refuse. */
typedef int (*secure_verify_fn)(void* ctx, const unsigned char id_pk[HS_ID_PK_BYTES]);

typedef struct {
    secure_role_t role;
    secure_state_t state;
    const char* error;           /* reason after SECURE_ERR, for logs */

    secure_emit_fn emit;
    secure_verify_fn verify;
    void* ctx;
    const identity_t* identity;  /* server only */

    unsigned char eph_pk[HS_KX_PK_BYTES];
    unsigned char eph_sk[crypto_kx_SECRETKEYBYTES];
    unsigned char peer_id_pk[HS_ID_PK_BYTES]; /* client: server identity */
    unsigned char rx_key[SECURE_KEY_BYTES];

    crypto_secretstream_xchacha20poly1305_state tx;
    crypto_secretstream_xchacha20poly1305_state rx;
    int tx_finished;
} secure_t;

void secure_server_init(secure_t* s, const identity_t* id,
                        secure_emit_fn emit, void* ctx);

/* Also emits CLIENT_HELLO. Returns 0 or -1. */
int secure_client_init(secure_t* s, secure_verify_fn verify,
                       secure_emit_fn emit, void* ctx);

/* Processes one received record body. On SECURE_DATA the plaintext is in
 * out[0..*out_len). `cap` must be at least `len`. */
int secure_recv(secure_t* s, const uint8_t* rec, size_t len,
                uint8_t* out, size_t cap, size_t* out_len);

/* Encrypts and emits one record. `final` marks the last record of the
 * stream. Returns 0 or -1. */
int secure_send(secure_t* s, const uint8_t* plain, size_t len, int final);

int secure_established(const secure_t* s);

/* Zeroes all key material. */
void secure_wipe(secure_t* s);

#endif
