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

int main(void) {
    if (sodium_init() < 0) return 1;
    RUN(accept_key_matches_rfc_example);
    RUN(accept_key_rejects_bad_keys);
    RUN(valid_upgrade_request_is_accepted);
    RUN(incomplete_upgrade_requests_are_rejected);
    return TEST_REPORT();
}
