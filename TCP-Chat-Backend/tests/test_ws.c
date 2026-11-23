#include "test.h"
#include "ws.h"

#include <sodium.h>

static http_request_t req;

static int parse(const char* s) {
    return http_parse_request((const uint8_t*)s, strlen(s), &req);
}

/* The example from RFC 6455 section 1.3. */
TEST(accept_key_matches_rfc_example) {
    char accept[WS_ACCEPT_SIZE];
    CHECK_EQ_INT(ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==", accept), 0);
    CHECK_EQ_STR(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST(accept_key_rejects_bad_keys) {
    char accept[WS_ACCEPT_SIZE];
    CHECK_EQ_INT(ws_accept_key("", accept), -1);
    CHECK_EQ_INT(ws_accept_key("short", accept), -1);
    CHECK_EQ_INT(ws_accept_key("!!!!!!!!!!!!!!!!!!!!!!==", accept), -1);
    CHECK_EQ_INT(ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==extra", accept), -1);
}

TEST(valid_upgrade_request_is_accepted) {
    char accept[WS_ACCEPT_SIZE];
    parse("GET /ws HTTP/1.1\r\n"
          "Host: localhost\r\n"
          "Upgrade: websocket\r\n"
          "Connection: keep-alive, Upgrade\r\n"
          "Sec-WebSocket-Version: 13\r\n"
          "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
    CHECK_EQ_INT(ws_check_upgrade(&req, accept), 0);
    CHECK_EQ_STR(accept, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");

    char resp[256];
    int n = ws_upgrade_response(resp, sizeof(resp), accept);
    CHECK(n > 0);
    CHECK(strstr(resp, "HTTP/1.1 101 Switching Protocols\r\n") == resp);
    CHECK(strstr(resp, "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != NULL);
    CHECK(strcmp(resp + n - 4, "\r\n\r\n") == 0);
}

TEST(incomplete_upgrade_requests_are_rejected) {
    char accept[WS_ACCEPT_SIZE];

    parse("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK_EQ_INT(ws_check_upgrade(&req, accept), -1);

    parse("POST / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
          "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
    CHECK_EQ_INT(ws_check_upgrade(&req, accept), -1);

    parse("GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
          "Sec-WebSocket-Version: 8\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
    CHECK_EQ_INT(ws_check_upgrade(&req, accept), -1);

    parse("GET / HTTP/1.1\r\nUpgrade: websocket\r\nConnection: keep-alive\r\n"
          "Sec-WebSocket-Version: 13\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n\r\n");
    CHECK_EQ_INT(ws_check_upgrade(&req, accept), -1);
}

/* Builds a masked client frame like a browser would. */
static size_t client_frame(uint8_t* out, int fin, int opcode, const uint8_t* data, size_t n) {
    static const uint8_t mask[4] = { 0x37, 0xFA, 0x21, 0x3D };
    size_t pos = 0;
    out[pos++] = (uint8_t)((fin ? 0x80 : 0) | opcode);
    if (n < 126) {
        out[pos++] = (uint8_t)(0x80 | n);
    } else if (n <= 0xFFFF) {
        out[pos++] = 0x80 | 126;
        out[pos++] = (uint8_t)(n >> 8);
        out[pos++] = (uint8_t)n;
    } else {
        out[pos++] = 0x80 | 127;
        for (int i = 0; i < 8; i++) out[pos++] = (uint8_t)((uint64_t)n >> (56 - 8 * i));
    }
    memcpy(out + pos, mask, 4);
    pos += 4;
    for (size_t i = 0; i < n; i++) out[pos + i] = data[i] ^ mask[i & 3];
    return pos + n;
}

/* RFC 6455 section 5.7: masked "Hello". */
TEST(decodes_rfc_masked_hello) {
    uint8_t f[] = { 0x81, 0x85, 0x37, 0xfa, 0x21, 0x3d, 0x7f, 0x9f, 0x4d, 0x51, 0x58 };
    ws_frame_t fr;
    CHECK_EQ_INT(ws_parse_frame(f, sizeof(f), 1024, &fr), (int)sizeof(f));
    CHECK_EQ_INT(fr.fin, 1);
    CHECK_EQ_INT(fr.opcode, WS_OP_TEXT);
    CHECK_EQ_INT(fr.len, 5);
    CHECK_EQ_MEM(fr.payload, "Hello", 5);
}

TEST(decodes_extended_lengths) {
    static uint8_t data[70000], buf[70100];
    ws_frame_t fr;
    for (size_t i = 0; i < sizeof(data); i++) data[i] = (uint8_t)i;

    size_t n = client_frame(buf, 1, WS_OP_BINARY, data, 300);         /* 16-bit */
    CHECK_EQ_INT(ws_parse_frame(buf, n, sizeof(data), &fr), (int)n);
    CHECK_EQ_INT(fr.len, 300);
    CHECK_EQ_MEM(fr.payload, data, 300);

    n = client_frame(buf, 1, WS_OP_BINARY, data, sizeof(data));       /* 64-bit */
    CHECK_EQ_INT(ws_parse_frame(buf, n, sizeof(data), &fr), (int)n);
    CHECK_EQ_INT(fr.len, sizeof(data));
    CHECK_EQ_MEM(fr.payload, data, sizeof(data));
}

TEST(partial_frames_need_more_bytes) {
    uint8_t buf[64];
    ws_frame_t fr;
    size_t n = client_frame(buf, 1, WS_OP_BINARY, (const uint8_t*)"abcdef", 6);
    for (size_t i = 0; i < n; i++)
        CHECK_EQ_INT(ws_parse_frame(buf, i, 1024, &fr), 0);
}

TEST(rejects_protocol_violations) {
    uint8_t buf[300];
    ws_frame_t fr;
    size_t n;

    uint8_t unmasked[] = { 0x82, 0x01, 'x' };
    CHECK_EQ_INT(ws_parse_frame(unmasked, sizeof(unmasked), 1024, &fr), -1);

    n = client_frame(buf, 1, WS_OP_BINARY, (const uint8_t*)"x", 1);
    buf[0] |= 0x40;                                              /* RSV1 */
    CHECK_EQ_INT(ws_parse_frame(buf, n, 1024, &fr), -1);

    n = client_frame(buf, 1, 0x3, (const uint8_t*)"x", 1);       /* reserved opcode */
    CHECK_EQ_INT(ws_parse_frame(buf, n, 1024, &fr), -1);

    n = client_frame(buf, 0, WS_OP_PING, (const uint8_t*)"x", 1);/* fragmented control */
    CHECK_EQ_INT(ws_parse_frame(buf, n, 1024, &fr), -1);

    static uint8_t big[200];
    n = client_frame(buf, 1, WS_OP_PING, big, 126);              /* control > 125 */
    CHECK_EQ_INT(ws_parse_frame(buf, n, 1024, &fr), -1);

    n = client_frame(buf, 1, WS_OP_BINARY, big, 200);            /* over our limit */
    CHECK_EQ_INT(ws_parse_frame(buf, n, 100, &fr), -1);

    uint8_t non_minimal[] = { 0x82, 0x80 | 126, 0x00, 0x05, 0, 0, 0, 0, 1, 2, 3, 4, 5 };
    CHECK_EQ_INT(ws_parse_frame(non_minimal, sizeof(non_minimal), 1024, &fr), -1);
}

TEST(encodes_server_frame_headers) {
    uint8_t h[WS_MAX_HEADER_SIZE];

    CHECK_EQ_INT(ws_frame_header(h, WS_OP_BINARY, 5), 2);
    CHECK_EQ_INT(h[0], 0x82);
    CHECK_EQ_INT(h[1], 5);

    CHECK_EQ_INT(ws_frame_header(h, WS_OP_BINARY, 300), 4);
    CHECK_EQ_INT(h[1], 126);
    CHECK_EQ_INT((h[2] << 8) | h[3], 300);

    CHECK_EQ_INT(ws_frame_header(h, WS_OP_BINARY, 70000), 10);
    CHECK_EQ_INT(h[1], 127);
    CHECK_EQ_INT((h[7] << 16) | (h[8] << 8) | h[9], 70000);

    uint8_t c[4];
    CHECK_EQ_INT(ws_close_frame(c, WS_CLOSE_NORMAL), 4);
    const uint8_t expected[] = { 0x88, 0x02, 0x03, 0xE8 };
    CHECK_EQ_MEM(c, expected, 4);
}

int main(void) {
    if (sodium_init() < 0) return 1;
    RUN(accept_key_matches_rfc_example);
    RUN(accept_key_rejects_bad_keys);
    RUN(valid_upgrade_request_is_accepted);
    RUN(incomplete_upgrade_requests_are_rejected);
    RUN(decodes_rfc_masked_hello);
    RUN(decodes_extended_lengths);
    RUN(partial_frames_need_more_bytes);
    RUN(rejects_protocol_violations);
    RUN(encodes_server_frame_headers);
    return TEST_REPORT();
}
