#ifndef TEST_H
#define TEST_H

/*
 * Minimal unit test helpers.
 *
 * Usage:
 *   TEST(name) { CHECK(expr); CHECK_EQ_INT(a, b); }
 *   int main(void) { RUN(name); return TEST_REPORT(); }
 */

#include <stdio.h>
#include <string.h>

static int test_failures = 0;
static int test_checks = 0;

#define TEST(name) static void name(void)

#define RUN(name)                                   \
    do {                                            \
        int before_ = test_failures;                \
        name();                                     \
        printf("  %s %s\n",                         \
               test_failures == before_ ? "PASS" : "FAIL", #name); \
    } while (0)

#define CHECK(expr)                                                  \
    do {                                                             \
        test_checks++;                                               \
        if (!(expr)) {                                               \
            test_failures++;                                         \
            fprintf(stderr, "    %s:%d: CHECK(%s) failed\n",         \
                    __FILE__, __LINE__, #expr);                      \
        }                                                            \
    } while (0)

#define CHECK_EQ_INT(a, b)                                           \
    do {                                                             \
        long long a_ = (long long)(a), b_ = (long long)(b);          \
        test_checks++;                                               \
        if (a_ != b_) {                                              \
            test_failures++;                                         \
            fprintf(stderr, "    %s:%d: %s == %s failed (%lld != %lld)\n", \
                    __FILE__, __LINE__, #a, #b, a_, b_);             \
        }                                                            \
    } while (0)

#define CHECK_EQ_STR(a, b)                                           \
    do {                                                             \
        const char *a_ = (a), *b_ = (b);                             \
        test_checks++;                                               \
        if (strcmp(a_, b_) != 0) {                                   \
            test_failures++;                                         \
            fprintf(stderr, "    %s:%d: \"%s\" != \"%s\"\n",          \
                    __FILE__, __LINE__, a_, b_);                     \
        }                                                            \
    } while (0)

#define CHECK_EQ_MEM(a, b, n)                                        \
    do {                                                             \
        test_checks++;                                               \
        if (memcmp((a), (b), (n)) != 0) {                            \
            test_failures++;                                         \
            fprintf(stderr, "    %s:%d: memory %s != %s (%zu bytes)\n", \
                    __FILE__, __LINE__, #a, #b, (size_t)(n));        \
        }                                                            \
    } while (0)

#define TEST_REPORT()                                                \
    (printf("  %d checks, %d failures\n", test_checks, test_failures), \
     test_failures == 0 ? 0 : 1)

#endif
