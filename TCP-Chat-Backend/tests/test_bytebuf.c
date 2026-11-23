#include "test.h"
#include "bytebuf.h"

TEST(integers_are_written_big_endian) {
    uint8_t buf[15];
    bytebuf_t b;
    bb_init_writer(&b, buf, sizeof(buf));

    CHECK_EQ_INT(bb_put_u8(&b, 0xAB), 0);
    CHECK_EQ_INT(bb_put_u16(&b, 0x1234), 0);
    CHECK_EQ_INT(bb_put_u32(&b, 0xDEADBEEF), 0);
    CHECK_EQ_INT(bb_put_u64(&b, 0x0102030405060708ULL), 0);

    const uint8_t expected[] = {
        0xAB,
        0x12, 0x34,
        0xDE, 0xAD, 0xBE, 0xEF,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08,
    };
    CHECK_EQ_INT(b.pos, sizeof(expected));
    CHECK_EQ_MEM(buf, expected, sizeof(expected));
}

TEST(integers_round_trip) {
    uint8_t buf[32];
    bytebuf_t w, r;
    bb_init_writer(&w, buf, sizeof(buf));
    bb_put_u8(&w, 7);
    bb_put_u16(&w, 65535);
    bb_put_u32(&w, 4000000000u);
    bb_put_u64(&w, 18000000000000000000ULL);

    uint8_t a; uint16_t b; uint32_t c; uint64_t d;
    bb_init_reader(&r, buf, w.pos);
    CHECK_EQ_INT(bb_get_u8(&r, &a), 0);
    CHECK_EQ_INT(bb_get_u16(&r, &b), 0);
    CHECK_EQ_INT(bb_get_u32(&r, &c), 0);
    CHECK_EQ_INT(bb_get_u64(&r, &d), 0);
    CHECK_EQ_INT(a, 7);
    CHECK_EQ_INT(b, 65535);
    CHECK(c == 4000000000u);
    CHECK(d == 18000000000000000000ULL);
    CHECK_EQ_INT(bb_remaining(&r), 0);
}

TEST(strings_are_length_prefixed) {
    uint8_t buf[16];
    bytebuf_t b;
    bb_init_writer(&b, buf, sizeof(buf));

    CHECK_EQ_INT(bb_put_str(&b, "hey"), 0);
    const uint8_t expected[] = { 0x00, 0x03, 'h', 'e', 'y' };
    CHECK_EQ_INT(b.pos, sizeof(expected));
    CHECK_EQ_MEM(buf, expected, sizeof(expected));

    char out[8];
    bytebuf_t r;
    bb_init_reader(&r, buf, b.pos);
    CHECK_EQ_INT(bb_get_str(&r, out, sizeof(out)), 0);
    CHECK_EQ_STR(out, "hey");
}

TEST(writer_overflow_is_sticky) {
    uint8_t buf[3];
    bytebuf_t b;
    bb_init_writer(&b, buf, sizeof(buf));

    CHECK_EQ_INT(bb_put_u16(&b, 1), 0);
    CHECK_EQ_INT(bb_put_u16(&b, 2), -1);   /* needs 2, only 1 left */
    CHECK(b.error);
    CHECK_EQ_INT(bb_put_u8(&b, 3), -1);    /* would fit, but error is sticky */
    CHECK_EQ_INT(b.pos, 2);
}

TEST(reader_rejects_truncated_input) {
    const uint8_t data[] = { 0x00, 0x05, 'a', 'b' }; /* claims 5 bytes, has 2 */
    bytebuf_t r;
    char out[16];
    bb_init_reader(&r, data, sizeof(data));

    CHECK_EQ_INT(bb_get_str(&r, out, sizeof(out)), -1);
    CHECK(r.error);

    uint32_t v;
    bb_init_reader(&r, data, 3);
    CHECK_EQ_INT(bb_get_u32(&r, &v), -1);
}

TEST(string_must_fit_output_buffer) {
    const uint8_t data[] = { 0x00, 0x04, 'a', 'b', 'c', 'd' };
    bytebuf_t r;
    char small[4]; /* needs 5 including NUL */
    bb_init_reader(&r, data, sizeof(data));
    CHECK_EQ_INT(bb_get_str(&r, small, sizeof(small)), -1);

    char ok[5];
    bb_init_reader(&r, data, sizeof(data));
    CHECK_EQ_INT(bb_get_str(&r, ok, sizeof(ok)), 0);
    CHECK_EQ_STR(ok, "abcd");
}

int main(void) {
    RUN(integers_are_written_big_endian);
    RUN(integers_round_trip);
    RUN(strings_are_length_prefixed);
    RUN(writer_overflow_is_sticky);
    RUN(reader_rejects_truncated_input);
    RUN(string_must_fit_output_buffer);
    return TEST_REPORT();
}
