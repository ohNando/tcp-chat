#ifndef WS_H
#define WS_H

/*
 * WebSocket (RFC 6455), server side.
 *
 * Opening handshake: the browser sends an HTTP GET with
 *   Upgrade: websocket, Connection: Upgrade, Sec-WebSocket-Version: 13,
 *   Sec-WebSocket-Key: <base64 of 16 random bytes>
 * and we answer "101 Switching Protocols" with
 *   Sec-WebSocket-Accept: base64(SHA1(key + WS_GUID))
 * which proves we really speak WebSocket (not a confused HTTP server).
 *
 * Frame layout (after the handshake):
 *
 *   0                   1                   2                   3
 *   +-+-+-+-+-------+-+-------------+-------------------------------+
 *   |F|R|R|R| opcode|M| Payload len |    Extended payload length    |
 *   |I|S|S|S|  (4)  |A|     (7)     |   (16 bits if len == 126,     |
 *   |N|V|V|V|       |S|             |    64 bits if len == 127)     |
 *   +-+-+-+-+-------+-+-------------+-------------------------------+
 *   | Masking key (4 bytes, only if MASK = 1) |   Payload data ...  |
 *   +-----------------------------------------+---------------------+
 *
 * Clients MUST mask every frame, servers MUST NOT.
 */

#include <stddef.h>
#include <stdint.h>

#include "http.h"

#define WS_GUID            "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define WS_ACCEPT_SIZE     29      /* 28 base64 chars + NUL */
#define WS_MAX_HEADER_SIZE 10      /* server frames have no mask */

typedef enum {
    WS_OP_CONT   = 0x0,
    WS_OP_TEXT   = 0x1,
    WS_OP_BINARY = 0x2,
    WS_OP_CLOSE  = 0x8,
    WS_OP_PING   = 0x9,
    WS_OP_PONG   = 0xA,
} ws_opcode_t;

/* Close status codes we use. */
#define WS_CLOSE_NORMAL       1000
#define WS_CLOSE_PROTOCOL     1002
#define WS_CLOSE_UNSUPPORTED  1003
#define WS_CLOSE_TOO_BIG      1009

typedef struct {
    int fin;
    int opcode;
    uint8_t* payload;   /* points into the input buffer, already unmasked */
    size_t len;
} ws_frame_t;

/* Parses one client frame from buf[0..len) and unmasks it in place.
 * Returns the number of bytes the frame occupies, 0 if more bytes are
 * needed, or -1 on a protocol violation (unmasked frame, reserved bits,
 * unknown opcode, fragmented or oversized control frame, payload larger
 * than max_payload). */
int ws_parse_frame(uint8_t* buf, size_t len, size_t max_payload, ws_frame_t* f);

/* Computes Sec-WebSocket-Accept for a client key. The key must be the
 * base64 encoding of exactly 16 bytes. Returns 0 or -1. */
int ws_accept_key(const char* client_key, char out[WS_ACCEPT_SIZE]);

/* Validates an upgrade request and computes the accept key.
 * Returns 0, or -1 if the request is not a valid WebSocket upgrade. */
int ws_check_upgrade(const http_request_t* req, char accept[WS_ACCEPT_SIZE]);

/* Writes the "101 Switching Protocols" response. Returns its length or -1. */
int ws_upgrade_response(char* out, size_t cap, const char accept[WS_ACCEPT_SIZE]);

#endif
