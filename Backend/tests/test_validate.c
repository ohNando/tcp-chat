#include "test.h"
#include "validate.h"
#include "message.h"

#define U8(s) utf8_valid((s), sizeof(s) - 1)

TEST(utf8_accepts_valid_sequences) {
    CHECK(U8(""));
    CHECK(U8("ascii only"));
    CHECK(U8("çğıöşü ÇĞİÖŞÜ"));         /* 2-byte */
    CHECK(U8("\xE2\x82\xAC"));          /* € 3-byte */
    CHECK(U8("\xF0\x9F\x98\x80"));      /* 😀 4-byte */
    CHECK(U8("\xF4\x8F\xBF\xBF"));      /* U+10FFFF */
}

TEST(utf8_rejects_invalid_sequences) {
    CHECK(!U8("\x80"));                 /* lone continuation */
    CHECK(!U8("\xC3"));                 /* truncated 2-byte */
    CHECK(!U8("\xE2\x82"));             /* truncated 3-byte */
    CHECK(!U8("\xC0\xAF"));             /* overlong '/' */
    CHECK(!U8("\xE0\x80\xAF"));         /* overlong 3-byte */
    CHECK(!U8("\xED\xA0\x80"));         /* surrogate U+D800 */
    CHECK(!U8("\xF4\x90\x80\x80"));     /* > U+10FFFF */
    CHECK(!U8("\xFF"));                 /* invalid lead */
    CHECK(!U8("ok\xC3("));              /* bad continuation */
}

TEST(text_rules) {
    char big[TEXT_MAX_LEN + 2];

    CHECK(text_valid("hello"));
    CHECK(text_valid("tab\tis fine"));
    CHECK(!text_valid(""));
    CHECK(!text_valid("new\nline"));
    CHECK(!text_valid("\x1b[2J clear screen"));
    CHECK(!text_valid("del\x7f"));

    memset(big, 'a', TEXT_MAX_LEN);
    big[TEXT_MAX_LEN] = '\0';
    CHECK(text_valid(big));
    big[TEXT_MAX_LEN] = 'a';
    big[TEXT_MAX_LEN + 1] = '\0';
    CHECK(!text_valid(big));
}

TEST(name_rules) {
    CHECK(name_valid("ali"));
    CHECK(name_valid("Emir_Han-42.x"));
    CHECK(name_valid("Şükrü"));
    CHECK(name_valid("ab"));
    CHECK(!name_valid("a"));
    CHECK(!name_valid(""));
    CHECK(!name_valid("has space"));
    CHECK(!name_valid("semi;colon"));
    CHECK(!name_valid("esc\x1b"));
    CHECK(!name_valid("bad\xC3("));
    CHECK(name_valid("abcdefghijklmnopqrstuvwx"));   /* 24 */
    CHECK(!name_valid("abcdefghijklmnopqrstuvwxy")); /* 25 */
}

TEST(name_equality_ignores_ascii_case) {
    CHECK(name_equal("Ali", "ali"));
    CHECK(name_equal("ALI", "aLi"));
    CHECK(!name_equal("ali", "alii"));
    CHECK(!name_equal("ali", "veli"));
}

int main(void) {
    RUN(utf8_accepts_valid_sequences);
    RUN(utf8_rejects_invalid_sequences);
    RUN(text_rules);
    RUN(name_rules);
    RUN(name_equality_ignores_ascii_case);
    return TEST_REPORT();
}
