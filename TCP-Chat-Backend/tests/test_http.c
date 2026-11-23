#include "test.h"
#include "http.h"

static http_request_t req;

static int parse(const char* s) {
    return http_parse_request((const uint8_t*)s, strlen(s), &req);
}

TEST(parses_request_line_and_headers) {
    const char* r = "GET /chat HTTP/1.1\r\n"
                    "Host: localhost:8081\r\n"
                    "Upgrade:   websocket  \r\n"
                    "Connection: keep-alive, Upgrade\r\n"
                    "\r\n";
    CHECK_EQ_INT(parse(r), (int)strlen(r));
    CHECK_EQ_STR(req.method, "GET");
    CHECK_EQ_STR(req.target, "/chat");
    CHECK_EQ_STR(req.version, "HTTP/1.1");
    CHECK_EQ_INT(req.header_count, 3);
    CHECK_EQ_STR(http_header(&req, "upgrade"), "websocket");
    CHECK(http_header(&req, "Missing") == NULL);
    CHECK(http_header_has_token(&req, "connection", "upgrade"));
    CHECK(http_header_has_token(&req, "Connection", "KEEP-ALIVE"));
    CHECK(!http_header_has_token(&req, "Connection", "upgr"));
}

TEST(incomplete_request_needs_more) {
    CHECK_EQ_INT(parse("GET / HTTP/1.1\r\nHost: x\r\n"), 0);
    CHECK_EQ_INT(parse("GET / HTT"), 0);
}

TEST(returns_size_without_following_bytes) {
    const char* r = "GET / HTTP/1.1\r\n\r\nEXTRA";
    CHECK_EQ_INT(parse(r), (int)strlen(r) - 5);
}

TEST(rejects_malformed_requests) {
    CHECK_EQ_INT(parse("GET\r\n\r\n"), -1);
    CHECK_EQ_INT(parse("GET / HTTP/2.0\r\n\r\n"), -1);
    CHECK_EQ_INT(parse("GET http://evil/ HTTP/1.1\r\n\r\n"), -1);
    CHECK_EQ_INT(parse("G(T / HTTP/1.1\r\n\r\n"), -1);
    CHECK_EQ_INT(parse("GET / HTTP/1.1\r\nNoColon\r\n\r\n"), -1);
    CHECK_EQ_INT(parse("GET / HTTP/1.1\r\nBad Name: x\r\n\r\n"), -1);
    CHECK_EQ_INT(parse("GET / HTTP/1.1\r\nA: b\r\n folded\r\n\r\n"), -1);
    CHECK_EQ_INT(parse("GET / / HTTP/1.1\r\n\r\n"), -1);
}

TEST(rejects_oversized_request) {
    static char big[HTTP_MAX_REQUEST + 16];
    memset(big, 'a', sizeof(big) - 1);
    memcpy(big, "GET / HTTP/1.1\r\nX: ", 19);
    CHECK_EQ_INT(http_parse_request((const uint8_t*)big, sizeof(big) - 1, &req), -1);
}

TEST(rejects_too_many_headers) {
    char r[4096] = "GET / HTTP/1.1\r\n";
    for (int i = 0; i <= HTTP_MAX_HEADERS; i++) strcat(r, "A: b\r\n");
    strcat(r, "\r\n");
    CHECK_EQ_INT(parse(r), -1);
}

int main(void) {
    RUN(parses_request_line_and_headers);
    RUN(incomplete_request_needs_more);
    RUN(returns_size_without_following_bytes);
    RUN(rejects_malformed_requests);
    RUN(rejects_oversized_request);
    RUN(rejects_too_many_headers);
    return TEST_REPORT();
}
