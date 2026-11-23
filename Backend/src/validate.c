#include "validate.h"
#include "message.h"

#include <ctype.h>
#include <stdint.h>
#include <string.h>

int utf8_valid(const char* str, size_t n) {
    const unsigned char* s = (const unsigned char*)str;
    size_t i = 0;

    while (i < n) {
        unsigned char c = s[i];
        uint32_t cp;
        size_t extra;

        if (c < 0x80) { i++; continue; }
        else if ((c & 0xE0) == 0xC0) { cp = c & 0x1F; extra = 1; }
        else if ((c & 0xF0) == 0xE0) { cp = c & 0x0F; extra = 2; }
        else if ((c & 0xF8) == 0xF0) { cp = c & 0x07; extra = 3; }
        else return 0; /* stray continuation byte or invalid lead */

        if (i + extra >= n) return 0; /* sequence runs past the end */
        for (size_t k = 1; k <= extra; k++) {
            if ((s[i + k] & 0xC0) != 0x80) return 0;
            cp = (cp << 6) | (s[i + k] & 0x3F);
        }

        /* Reject overlong encodings, surrogates and out-of-range values. */
        if ((extra == 1 && cp < 0x80) ||
            (extra == 2 && cp < 0x800) ||
            (extra == 3 && cp < 0x10000)) return 0;
        if (cp >= 0xD800 && cp <= 0xDFFF) return 0;
        if (cp > 0x10FFFF) return 0;

        i += extra + 1;
    }
    return 1;
}

static int has_control_chars(const char* s, size_t n, int allow_tab) {
    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c == '\t' && allow_tab) continue;
        if (c < 0x20 || c == 0x7F) return 1;
    }
    return 0;
}

int text_valid(const char* s) {
    size_t n = strlen(s);
    if (n == 0 || n > TEXT_MAX_LEN) return 0;
    if (!utf8_valid(s, n)) return 0;
    if (has_control_chars(s, n, 1)) return 0;
    return 1;
}

int name_valid(const char* s) {
    size_t n = strlen(s);
    if (n < NAME_MIN_LEN || n > NAME_MAX_LEN) return 0;
    if (!utf8_valid(s, n)) return 0;

    for (size_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 0x80) continue; /* part of a multi-byte character */
        if (isalnum(c) || c == '_' || c == '-' || c == '.') continue;
        return 0;
    }
    return 1;
}

int name_equal(const char* a, const char* b) {
    while (*a && *b) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return 0;
        a++;
        b++;
    }
    return *a == *b;
}
