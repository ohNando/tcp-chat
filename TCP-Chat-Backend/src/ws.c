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
