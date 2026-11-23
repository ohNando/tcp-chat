#include "ws.h"
#include "sha1.h"

#include <sodium.h>
#include <stdio.h>
#include <string.h>

int ws_accept_key(const char* client_key, char out[WS_ACCEPT_SIZE]) {
    unsigned char raw[16];
    size_t raw_len;
    size_t key_len = strlen(client_key);

    if (key_len != 24) return -1;
    if (sodium_base642bin(raw, sizeof(raw), client_key, key_len, NULL, &raw_len, NULL,
                          sodium_base64_VARIANT_ORIGINAL) != 0 || raw_len != 16)
        return -1;

    sha1_ctx_t ctx;
    uint8_t digest[SHA1_DIGEST_SIZE];
    sha1_init(&ctx);
    sha1_update(&ctx, client_key, key_len);
    sha1_update(&ctx, WS_GUID, sizeof(WS_GUID) - 1);
    sha1_final(&ctx, digest);

    sodium_bin2base64(out, WS_ACCEPT_SIZE, digest, sizeof(digest),
                      sodium_base64_VARIANT_ORIGINAL);
    return 0;
}

int ws_check_upgrade(const http_request_t* req, char accept[WS_ACCEPT_SIZE]) {
    if (strcmp(req->method, "GET") != 0) return -1;
    if (strcmp(req->version, "HTTP/1.1") != 0) return -1;
    if (!http_header_has_token(req, "Upgrade", "websocket")) return -1;
    if (!http_header_has_token(req, "Connection", "upgrade")) return -1;

    const char* version = http_header(req, "Sec-WebSocket-Version");
    if (!version || strcmp(version, "13") != 0) return -1;

    const char* key = http_header(req, "Sec-WebSocket-Key");
    if (!key) return -1;
    return ws_accept_key(key, accept);
}

int ws_upgrade_response(char* out, size_t cap, const char accept[WS_ACCEPT_SIZE]) {
    int n = snprintf(out, cap,
                     "HTTP/1.1 101 Switching Protocols\r\n"
                     "Upgrade: websocket\r\n"
                     "Connection: Upgrade\r\n"
                     "Sec-WebSocket-Accept: %s\r\n"
                     "\r\n",
                     accept);
    return (n > 0 && (size_t)n < cap) ? n : -1;
}

static int opcode_known(int op) {
    return op == WS_OP_CONT || op == WS_OP_TEXT || op == WS_OP_BINARY ||
           op == WS_OP_CLOSE || op == WS_OP_PING || op == WS_OP_PONG;
}

int ws_parse_frame(uint8_t* buf, size_t len, size_t max_payload, ws_frame_t* f) {
    if (len < 2) return 0;

    int fin = buf[0] & 0x80;
    int rsv = buf[0] & 0x70;
    int opcode = buf[0] & 0x0F;
    int masked = buf[1] & 0x80;
    uint64_t plen = buf[1] & 0x7F;
    size_t pos = 2;

    /* No extensions are negotiated, so RSV bits must be zero. */
    if (rsv || !opcode_known(opcode)) return -1;
    if (!masked) return -1;

    int control = opcode & 0x8;
    if (control && (!fin || plen > 125)) return -1;

    if (plen == 126) {
        if (len < pos + 2) return 0;
        plen = ((uint64_t)buf[2] << 8) | buf[3];
        pos += 2;
        if (plen < 126) return -1;           /* must use the shortest form */
    } else if (plen == 127) {
        if (len < pos + 8) return 0;
        plen = 0;
        for (int i = 0; i < 8; i++) plen = (plen << 8) | buf[2 + i];
        pos += 8;
        if (plen >> 63) return -1;           /* most significant bit must be 0 */
        if (plen <= 0xFFFF) return -1;
    }

    if (plen > max_payload) return -1;
    if (len < pos + 4) return 0;
    uint8_t mask[4];
    memcpy(mask, buf + pos, 4);
    pos += 4;

    if (len - pos < plen) return 0;

    uint8_t* payload = buf + pos;
    for (uint64_t i = 0; i < plen; i++) payload[i] ^= mask[i & 3];

    f->fin = fin ? 1 : 0;
    f->opcode = opcode;
    f->payload = payload;
    f->len = (size_t)plen;
    return (int)(pos + plen);
}

size_t ws_frame_header(uint8_t out[WS_MAX_HEADER_SIZE], int opcode, size_t payload_len) {
    out[0] = (uint8_t)(0x80 | (opcode & 0x0F));
    if (payload_len < 126) {
        out[1] = (uint8_t)payload_len;
        return 2;
    }
    if (payload_len <= 0xFFFF) {
        out[1] = 126;
        out[2] = (uint8_t)(payload_len >> 8);
        out[3] = (uint8_t)payload_len;
        return 4;
    }
    out[1] = 127;
    uint64_t v = payload_len;
    for (int i = 0; i < 8; i++) out[2 + i] = (uint8_t)(v >> (56 - 8 * i));
    return 10;
}

size_t ws_close_frame(uint8_t out[4], uint16_t code) {
    out[0] = 0x80 | WS_OP_CLOSE;
    out[1] = 2;
    out[2] = (uint8_t)(code >> 8);
    out[3] = (uint8_t)code;
    return 4;
}
