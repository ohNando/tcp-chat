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
 *   CHAT      u64 timestamp_ms | str name | str text
 *   JOIN      str name
 *   LEAVE     str name
 *   USERS     u16 count | count x str name
 *   ERR       u16 code | str text
 *
 * Decoding is strict: truncated payloads and trailing bytes are rejected.
 */

#include <stddef.h>
#include <stdint.h>

#include "frame.h"

#define NAME_MAX_LEN  24
#define TEXT_MAX_LEN  2000
#define USERS_MAX     256

typedef enum {
    ERR_PROTOCOL          = 1,
    ERR_NAME_INVALID      = 2,
    ERR_NAME_TAKEN        = 3,
    ERR_NOT_REGISTERED    = 4,
    ERR_ALREADY_REGISTERED = 5,
    ERR_ROOM_FULL         = 6,
    ERR_TEXT_INVALID      = 7,
} error_code_t;

typedef struct {
    uint8_t type;
    uint64_t timestamp;                      /* CHAT */
    uint16_t code;                           /* ERR */
    char name[NAME_MAX_LEN + 1];             /* NAME, CHAT, JOIN, LEAVE */
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
