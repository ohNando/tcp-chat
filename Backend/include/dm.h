#ifndef DM_H
#define DM_H

/*
 * End-to-end encrypted private messages, shared by every C client.
 *
 * Each user has a long-term X25519 key pair (crypto_box) stored on the
 * device. A message to a peer is
 *
 *     box = crypto_box_easy(u64 sent_at | str text, nonce, peer_pk, my_sk)
 *
 * The server only relays `nonce` and `box`; it never sees the text. Because
 * both sides derive the same shared key, the sender can open its own copy.
 *
 * The safety number lets two people check out of band that the server
 * handed them each other's real keys.
 */

#include <stddef.h>
#include <stdint.h>

#include "message.h"

/* File: "TCHTUSR1" | 32-byte seed, mode 0600. */
#define USER_KEY_FILE_MAGIC "TCHTUSR1"

/* "12345 67890 ..." : 8 groups of 5 digits + NUL */
#define DM_SAFETY_STR_LEN (8 * 6)

typedef struct {
    uint8_t pk[DM_KEY_BYTES];
    uint8_t sk[DM_KEY_BYTES];
} user_key_t;

/* ~/.config/tcp-chat/user.key, or $TCPCHAT_USER_KEY. */
int user_key_path(char* out, size_t cap);

/* Loads the key, or creates it (and its directory) if missing. */
int user_key_load_or_create(user_key_t* key, const char* path, int* created);

void user_key_wipe(user_key_t* key);

/* Encrypts `text` for `peer_pk`. Fills a fresh random nonce and the box.
 * Returns 0, or -1 if the text breaks the chat text rules. */
int dm_seal(const user_key_t* me, const uint8_t peer_pk[DM_KEY_BYTES],
            uint64_t sent_at, const char* text,
            uint8_t nonce[DM_NONCE_BYTES], uint8_t box[DM_BOX_MAX], uint16_t* box_len);

/* Opens a box from (or, for our own echo, to) `peer_pk`. Returns 0, or -1
 * if it was forged, damaged, or decrypts to text that breaks the rules. */
int dm_open(const user_key_t* me, const uint8_t peer_pk[DM_KEY_BYTES],
            const uint8_t nonce[DM_NONCE_BYTES], const uint8_t* box, size_t box_len,
            uint64_t* sent_at, char text[TEXT_MAX_LEN + 1]);

/* Same for both people, whatever the order of the arguments. */
void dm_safety_number(const uint8_t a[DM_KEY_BYTES], const uint8_t b[DM_KEY_BYTES],
                      char out[DM_SAFETY_STR_LEN]);

#endif
