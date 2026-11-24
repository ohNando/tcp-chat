#include "test.h"
#include "framer.h"

/* Builds "len | body" into out, returns total size. */
static size_t make_record(uint8_t* out, const char* body) {
    size_t n = strlen(body);
    record_put_header(out, (uint32_t)n);
    memcpy(out + RECORD_HEADER_SIZE, body, n);
    return RECORD_HEADER_SIZE + n;
}

TEST(single_complete_record) {
    framer_t f;
    uint8_t wire[64];
    const uint8_t* body; size_t len;

    framer_init(&f, 128);
    size_t n = make_record(wire, "hello");
    CHECK_EQ_INT(framer_feed(&f, wire, n), n);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_INT(len, 5);
    CHECK_EQ_MEM(body, "hello", 5);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 0);
    CHECK_EQ_INT(framer_pending(&f), 0);
    framer_free(&f);
}

TEST(record_split_byte_by_byte) {
    framer_t f;
    uint8_t wire[64];
    const uint8_t* body; size_t len;

    framer_init(&f, 128);
    size_t n = make_record(wire, "split");
    for (size_t i = 0; i < n - 1; i++) {
        framer_feed(&f, wire + i, 1);
        CHECK_EQ_INT(framer_next(&f, &body, &len), 0);
    }
    framer_feed(&f, wire + n - 1, 1);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "split", 5);
    framer_free(&f);
}

TEST(merged_records_in_one_read) {
    framer_t f;
    uint8_t wire[128];
    const uint8_t* body; size_t len;

    framer_init(&f, 128);
    size_t n = make_record(wire, "one");
    n += make_record(wire + n, "two");
    n += make_record(wire + n, "");
    framer_feed(&f, wire, n);

    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "one", 3);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "two", 3);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_INT(len, 0);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 0);
    framer_free(&f);
}

TEST(record_followed_by_partial_record) {
    framer_t f;
    uint8_t wire[128];
    const uint8_t* body; size_t len;

    framer_init(&f, 128);
    size_t first = make_record(wire, "full");
    size_t second = make_record(wire + first, "later");
    framer_feed(&f, wire, first + 3);

    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "full", 4);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 0);

    framer_feed(&f, wire + first + 3, second - 3);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "later", 5);
    framer_free(&f);
}

TEST(oversized_record_is_rejected) {
    framer_t f;
    uint8_t hdr[RECORD_HEADER_SIZE];
    const uint8_t* body; size_t len;

    framer_init(&f, 16);
    record_put_header(hdr, 17);
    framer_feed(&f, hdr, sizeof(hdr));
    CHECK_EQ_INT(framer_next(&f, &body, &len), -1);
    framer_free(&f);
}

TEST(buffer_compacts_to_fit_max_record) {
    framer_t f;
    uint8_t wire[64];
    const uint8_t* body; size_t len;

    /* Capacity is exactly one max-size record. After consuming a small
     * record, a max-size record must still fit thanks to compaction. */
    framer_init(&f, 8);
    size_t n = make_record(wire, "abc");
    framer_feed(&f, wire, n);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);

    n = make_record(wire, "12345678");
    CHECK_EQ_INT(framer_feed(&f, wire, n), n);
    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_MEM(body, "12345678", 8);
    framer_free(&f);
}

TEST(peek_and_consume_raw_bytes) {
    framer_t f;
    size_t len;
    framer_init(&f, 32);
    framer_feed(&f, "GET / HTTP", 10);

    uint8_t* p = framer_peek(&f, &len);
    CHECK_EQ_INT(len, 10);
    CHECK_EQ_MEM(p, "GET / HTTP", 10);

    framer_consume(&f, 4);
    p = framer_peek(&f, &len);
    CHECK_EQ_INT(len, 6);
    CHECK_EQ_MEM(p, "/ HTTP", 6);

    framer_consume(&f, 100); /* clamps */
    CHECK_EQ_INT(framer_pending(&f), 0);
    framer_free(&f);
}

TEST(buffer_grows_for_large_records_and_shrinks_when_empty) {
    framer_t f;
    const uint8_t* body; size_t len;
    static uint8_t wire[RECORD_HEADER_SIZE + 50000];

    framer_init(&f, 60000);
    CHECK_EQ_INT(f.cap, FRAMER_INITIAL_CAP);

    record_put_header(wire, 50000);
    for (size_t i = 0; i < 50000; i++) wire[RECORD_HEADER_SIZE + i] = (uint8_t)i;
    CHECK_EQ_INT(framer_feed(&f, wire, sizeof(wire)), sizeof(wire));
    CHECK(f.cap >= sizeof(wire));
    CHECK(f.cap <= f.max_cap);

    CHECK_EQ_INT(framer_next(&f, &body, &len), 1);
    CHECK_EQ_INT(len, 50000);
    CHECK_EQ_MEM(body, wire + RECORD_HEADER_SIZE, 50000);

    size_t avail;
    framer_space(&f, &avail);                 /* empty again: shrinks */
    CHECK_EQ_INT(f.cap, FRAMER_INITIAL_CAP);
    framer_free(&f);
}

TEST(full_buffer_at_max_size_reports_no_space) {
    framer_t f;
    size_t avail;
    uint8_t junk[64] = { 0 };
    framer_init(&f, 16);                      /* max_cap = 20 */
    CHECK_EQ_INT(framer_feed(&f, junk, sizeof(junk)), 20);
    framer_space(&f, &avail);
    CHECK_EQ_INT(avail, 0);
    framer_free(&f);
}

int main(void) {
    RUN(single_complete_record);
    RUN(record_split_byte_by_byte);
    RUN(merged_records_in_one_read);
    RUN(record_followed_by_partial_record);
    RUN(oversized_record_is_rejected);
    RUN(buffer_compacts_to_fit_max_record);
    RUN(peek_and_consume_raw_bytes);
    RUN(buffer_grows_for_large_records_and_shrinks_when_empty);
    RUN(full_buffer_at_max_size_reports_no_space);
    return TEST_REPORT();
}
