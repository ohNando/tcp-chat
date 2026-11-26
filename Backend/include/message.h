#ifndef MESSAGE_H
#define MESSAGE_H

/*
 * Typed messages carried inside frames. Payload layouts (str = u16 len + bytes):
 *
 *   NAME      str name
 *   MSG       str text
 *   USERS_REQ (empty)
 *   QUIT      (empty)
 *   PING/PONG (empty)
 *   WELCOME   str text
 *   CHAT      u64 id | u64 timestamp_ms | str name | str text
 *   JOIN      str name
 *   LEAVE     str name
 *   USERS     u16 count | count x str name
 *   ERR       u16 code | str text
 *   TYPING       u8 active                  (client -> server)
 *   USER_TYPING  str name | u8 active       (server -> client)
 *   KEY          key[32]                    (client -> server)
 *   PEER_KEY     str name | key[32]         (server -> client)
 *   DM           str to | nonce[24] | u16 len | box[len]          (client -> server)
 *   DM_RECV      u64 ts | str from | str to | nonce[24] | u16 len | box[len]
 *   HISTORY_REQ  u64 before_id | u16 limit   (client -> server; before_id 0 = newest)
 *   HISTORY_END  u8 more                     (server -> client, ends a history batch)
 *
 * DM boxes are end-to-end encrypted between the two users (see dm.h); the
 * server only relays them.
 *
 * Decoding is strict: truncated payloads and trailing bytes are rejected.
 */

#include <stddef.h>
#include <stdint.h>

#include "frame.h"

#define NAME_MAX_LEN  24
#define TEXT_MAX_LEN  2000
#define USERS_MAX     256

#define DM_KEY_BYTES   32
#define DM_NONCE_BYTES 24
/* Box = Poly1305 tag (16) + plaintext (u64 sent_at + str text). */
#define DM_BOX_MAX     (16 + 8 + 2 + TEXT_MAX_LEN)

typedef enum {
    ERR_PROTOCOL          = 1,
    ERR_NAME_INVALID      = 2,
    ERR_NAME_TAKEN        = 3,
    ERR_NOT_REGISTERED    = 4,
    ERR_ALREADY_REGISTERED = 5,
    ERR_ROOM_FULL         = 6,
    ERR_TEXT_INVALID      = 7,
    ERR_NO_SUCH_USER      = 8,
    ERR_NO_KEY            = 9,
} error_code_t;

typedef struct {
    uint8_t type;
    uint8_t flags;                           /* FLAG_* from frame.h */
    uint64_t id;                             /* CHAT (0 = not stored), HISTORY_REQ (before) */
    uint64_t timestamp;                      /* CHAT, DM_RECV */
    uint16_t limit;                          /* HISTORY_REQ */
    uint8_t more;                            /* HISTORY_END: older messages exist */
    uint16_t code;                           /* ERR */
    uint8_t active;                          /* TYPING, USER_TYPING */
    char name[NAME_MAX_LEN + 1];             /* NAME, CHAT, JOIN, LEAVE, PEER_KEY, DM_RECV (from) */
    char to[NAME_MAX_LEN + 1];               /* DM, DM_RECV */
    uint8_t key[DM_KEY_BYTES];               /* KEY, PEER_KEY */
    uint8_t nonce[DM_NONCE_BYTES];           /* DM, DM_RECV */
    uint16_t box_len;                        /* DM, DM_RECV */
    uint8_t box[DM_BOX_MAX];                 /* DM, DM_RECV */
    char text[TEXT_MAX_LEN + 1];             /* MSG, WELCOME, CHAT, ERR */
    uint16_t user_count;                     /* USERS */
    char users[USERS_MAX][NAME_MAX_LEN + 1]; /* USERS */
} message_t;

/* Zeroes the message and sets its type. */
void message_init(message_t* m, uint8_t type);

/* Encodes header + payload into `out`. Returns total bytes or -1. */
int message_encode(const message_t* m, uint32_t seq, uint8_t* out, size_t cap);

/* Decodes a complete frame (header + payload). Returns 0 or -1.
 * `seq` may be NULL. */
int message_decode(const uint8_t* frame, size_t len, message_t* m, uint32_t* seq);

#endif
