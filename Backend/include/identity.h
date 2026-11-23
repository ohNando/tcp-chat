#ifndef IDENTITY_H
#define IDENTITY_H

/*
 * Long-term server identity: an Ed25519 signing key pair.
 *
 * The server signs every handshake with it, and clients remember the
 * public key (trust on first use). Losing or regenerating the key makes
 * every client see a "fingerprint changed" warning.
 *
 * File format (40 bytes):  "TCHTKEY1" | 32-byte seed
 * The file is created with mode 0600 and never overwritten.
 */

#include <stddef.h>
#include <sodium.h>

#define IDENTITY_PK_BYTES   crypto_sign_PUBLICKEYBYTES   /* 32 */
#define IDENTITY_SK_BYTES   crypto_sign_SECRETKEYBYTES   /* 64 */
#define IDENTITY_FILE_MAGIC "TCHTKEY1"
#define IDENTITY_DEFAULT_PATH "server_identity.key"

/* "XXXX:XXXX:..." : 16 groups of 4 hex digits + NUL */
#define FINGERPRINT_STR_LEN (16 * 5)

typedef struct {
    unsigned char pk[IDENTITY_PK_BYTES];
    unsigned char sk[IDENTITY_SK_BYTES];
} identity_t;

/* Loads the key from `path`, or creates a new one if the file does not
 * exist (`*created` is then set to 1). Returns 0 or -1 (message printed). */
int identity_load_or_create(identity_t* id, const char* path, int* created);

void identity_wipe(identity_t* id);

/* BLAKE2b-256 of the public key, formatted for humans. */
void identity_fingerprint(const unsigned char pk[IDENTITY_PK_BYTES],
                          char out[FINGERPRINT_STR_LEN]);

#endif
