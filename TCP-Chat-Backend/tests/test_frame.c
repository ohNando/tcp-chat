#include "test.h"
#include "frame.h"

TEST(header_has_expected_wire_layout) {
    frame_header_t h = { .version = FRAME_VERSION, .type = FT_CHAT,
                         .flags = 0, .seq = 0x01020304, .len = 0x10 };
    uint8_t out[FRAME_HEADER_SIZE];

    CHECK_EQ_INT(frame_header_encode(&h, out, sizeof(out)), 0);
    const uint8_t expected[] = {
        FRAME_VERSION, FT_CHAT, 0x00, 0x00,
        0x01, 0x02, 0x03, 0x04,
        0x00, 0x00, 0x00, 0x10,
    };
    CHECK_EQ_MEM(out, expected, sizeof(expected));
}

TEST(header_round_trips) {
    frame_header_t in = { FRAME_VERSION, FT_MSG, 0, 42, 100 }, out;
    uint8_t buf[FRAME_HEADER_SIZE];

    frame_header_encode(&in, buf, sizeof(buf));
    CHECK_EQ_INT(frame_header_decode(buf, sizeof(buf), &out), 0);
    CHECK_EQ_INT(out.type, FT_MSG);
    CHECK_EQ_INT(out.seq, 42);
    CHECK_EQ_INT(out.len, 100);
}

TEST(encode_fails_on_small_buffer) {
    frame_header_t h = { FRAME_VERSION, FT_MSG, 0, 0, 0 };
    uint8_t buf[FRAME_HEADER_SIZE - 1];
    CHECK_EQ_INT(frame_header_encode(&h, buf, sizeof(buf)), -1);
}

TEST(decode_rejects_invalid_headers) {
    frame_header_t h = { FRAME_VERSION, FT_MSG, 0, 0, 0 };
    uint8_t buf[FRAME_HEADER_SIZE];

    /* truncated */
    frame_header_encode(&h, buf, sizeof(buf));
    CHECK_EQ_INT(frame_header_decode(buf, FRAME_HEADER_SIZE - 1, &h), -1);

    /* wrong version */
    frame_header_encode(&h, buf, sizeof(buf));
    buf[0] = 99;
    CHECK_EQ_INT(frame_header_decode(buf, sizeof(buf), &h), -1);

    /* unknown type */
    frame_header_encode(&h, buf, sizeof(buf));
    buf[1] = 0x7F;
    CHECK_EQ_INT(frame_header_decode(buf, sizeof(buf), &h), -1);

    /* reserved byte set */
    frame_header_encode(&h, buf, sizeof(buf));
    buf[3] = 1;
    CHECK_EQ_INT(frame_header_decode(buf, sizeof(buf), &h), -1);

    /* payload too large */
    h.len = FRAME_MAX_PAYLOAD + 1;
    frame_header_encode(&h, buf, sizeof(buf));
    CHECK_EQ_INT(frame_header_decode(buf, sizeof(buf), &h), -1);
}

int main(void) {
    RUN(header_has_expected_wire_layout);
    RUN(header_round_trips);
    RUN(encode_fails_on_small_buffer);
    RUN(decode_rejects_invalid_headers);
    return TEST_REPORT();
}
