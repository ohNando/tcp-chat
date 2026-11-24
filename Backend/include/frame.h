#ifndef FRAME_H
#define FRAME_H

/*
 * Application frame header (12 bytes, big-endian):
 *
 *   0       1       2       3       4               8               12
 *   +-------+-------+-------+-------+---------------+---------------+---------
 *   |  ver  | type  | flags |  rsv  |   seq (u32)   |   len (u32)   | payload
 *   +-------+-------+-------+-------+---------------+---------------+---------
 *
 * `len` is the payload length. `seq` is a per-sender counter, useful for
 * debugging and ordering checks. `rsv` must be zero.
 */

#include <stddef.h>
#include <stdint.h>

#define FRAME_VERSION      1
#define FRAME_HEADER_SIZE  12
#define FRAME_MAX_PAYLOAD  (64 * 1024)
#define FRAME_MAX_SIZE     (FRAME_HEADER_SIZE + FRAME_MAX_PAYLOAD)

typedef enum {
    /* client -> server */
    FT_NAME      = 0x01,
    FT_MSG       = 0x02,
    FT_USERS_REQ = 0x03,
    FT_QUIT      = 0x04,
    FT_PING      = 0x05,
    FT_PONG      = 0x06,

    /* server -> client */
    FT_WELCOME   = 0x81,
    FT_CHAT      = 0x82,
    FT_JOIN      = 0x83,
    FT_LEAVE     = 0x84,
    FT_USERS     = 0x85,
    FT_ERR       = 0x86,
} frame_type_t;

/* Frame flags (header byte 2). Unknown flags are ignored by receivers. */
#define FLAG_HISTORY 0x01   /* CHAT replayed from history, not live */

typedef struct {
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint32_t seq;
    uint32_t len;
} frame_header_t;

int frame_type_known(uint8_t type);
const char* frame_type_name(uint8_t type);

/* Writes exactly FRAME_HEADER_SIZE bytes. Returns 0 or -1. */
int frame_header_encode(const frame_header_t* h, uint8_t* out, size_t cap);

/* Parses and validates a header (version, reserved byte, known type,
 * payload limit). Returns 0 or -1. */
int frame_header_decode(const uint8_t* in, size_t len, frame_header_t* h);

#endif
