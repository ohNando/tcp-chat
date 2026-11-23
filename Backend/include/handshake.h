#ifndef HANDSHAKE_H
#define HANDSHAKE_H

/*
 * Handshake messages (sent as plain records before encryption starts).
 *
 *   CLIENT_HELLO  "TCHT" | ver u8 | client_eph_pk[32]
 *   SERVER_HELLO  "TCHT" | ver u8 | server_eph_pk[32] | server_id_pk[32] | sig[64]
 *
 * `sig` is the server's Ed25519 signature over the transcript:
 *
 *   "TCHT-handshake-v1" | client_eph_pk | server_eph_pk | server_id_pk
 *
 * The context string keeps this signature from being valid for anything
 * else; covering both ephemeral keys binds it to this one session, so an
 * attacker can neither replay an old SERVER_HELLO nor swap keys in transit.
 */

#include <stddef.h>
#include <stdint.h>
#include <sodium.h>

#define HS_MAGIC          "TCHT"
#define HS_MAGIC_LEN      4
#define HS_VERSION        1
#define HS_CONTEXT        "TCHT-handshake-v1"
#define HS_CONTEXT_LEN    (sizeof(HS_CONTEXT) - 1)

#define HS_KX_PK_BYTES    crypto_kx_PUBLICKEYBYTES     /* 32 */
#define HS_ID_PK_BYTES    crypto_sign_PUBLICKEYBYTES   /* 32 */
#define HS_SIG_BYTES      crypto_sign_BYTES            /* 64 */

#define CLIENT_HELLO_LEN  (HS_MAGIC_LEN + 1 + HS_KX_PK_BYTES)
#define SERVER_HELLO_LEN  (HS_MAGIC_LEN + 1 + HS_KX_PK_BYTES + HS_ID_PK_BYTES + HS_SIG_BYTES)
#define HS_TRANSCRIPT_LEN (HS_CONTEXT_LEN + 2 * HS_KX_PK_BYTES + HS_ID_PK_BYTES)

typedef struct {
    uint8_t version;
    unsigned char eph_pk[HS_KX_PK_BYTES];
} client_hello_t;

typedef struct {
    uint8_t version;
    unsigned char eph_pk[HS_KX_PK_BYTES];
    unsigned char id_pk[HS_ID_PK_BYTES];
    unsigned char sig[HS_SIG_BYTES];
} server_hello_t;

/* Return the encoded length, or -1. */
int client_hello_encode(const client_hello_t* h, uint8_t* out, size_t cap);
int server_hello_encode(const server_hello_t* h, uint8_t* out, size_t cap);

/* Strict: exact length, magic and version must match. Return 0 or -1. */
int client_hello_decode(const uint8_t* in, size_t len, client_hello_t* h);
int server_hello_decode(const uint8_t* in, size_t len, server_hello_t* h);

void handshake_transcript(uint8_t out[HS_TRANSCRIPT_LEN],
                          const unsigned char client_eph_pk[HS_KX_PK_BYTES],
                          const unsigned char server_eph_pk[HS_KX_PK_BYTES],
                          const unsigned char server_id_pk[HS_ID_PK_BYTES]);

#endif
