#include "test.h"
#include "ratelimit.h"

TEST(a_burst_is_allowed_then_limited) {
    bucket_t b = { 0 };
    int ok = 0;
    for (int i = 0; i < 30; i++) ok += bucket_take(&b, 5, 20, 1000);
    CHECK_EQ_INT(ok, 20);
    CHECK_EQ_INT(bucket_take(&b, 5, 20, 1000), 0);
}

TEST(the_bucket_refills_at_the_rate) {
    bucket_t b = { 0 };
    while (bucket_take(&b, 5, 20, 1000)) {}
    CHECK_EQ_INT(bucket_take(&b, 5, 20, 1100), 0);   /* 0.5 token */
    CHECK_EQ_INT(bucket_take(&b, 5, 20, 1200), 1);   /* 1 token after 200 ms */
    CHECK_EQ_INT(bucket_take(&b, 5, 20, 1200), 0);
}

TEST(a_steady_human_pace_is_never_limited) {
    bucket_t b = { 0 };
    int ok = 0;
    /* 4 messages a second for a minute, under the 5/s rate */
    for (int i = 0; i < 240; i++) ok += bucket_take(&b, 5, 20, 1000 + (uint64_t)i * 250);
    CHECK_EQ_INT(ok, 240);
}

TEST(refill_is_capped_at_the_burst) {
    bucket_t b = { 0 };
    bucket_take(&b, 5, 20, 1000);
    /* An hour of silence does not bank thousands of tokens. */
    int ok = 0;
    for (int i = 0; i < 100; i++) ok += bucket_take(&b, 5, 20, 3600 * 1000);
    CHECK_EQ_INT(ok, 20);
}

TEST(zero_rate_disables_the_limit) {
    bucket_t b = { 0 };
    int ok = 0;
    for (int i = 0; i < 1000; i++) ok += bucket_take(&b, 0, 0, 1000);
    CHECK_EQ_INT(ok, 1000);
}

int main(void) {
    RUN(a_burst_is_allowed_then_limited);
    RUN(the_bucket_refills_at_the_rate);
    RUN(a_steady_human_pace_is_never_limited);
    RUN(refill_is_capped_at_the_burst);
    RUN(zero_rate_disables_the_limit);
    return TEST_REPORT();
}
