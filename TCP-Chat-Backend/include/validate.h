#ifndef VALIDATE_H
#define VALIDATE_H

/*
 * Content rules shared by server and clients.
 *
 * Control characters are rejected in every user supplied string: a raw
 * ESC byte would let one user inject ANSI escape sequences into everyone
 * else's terminal.
 */

#include <stddef.h>

/* Strict UTF-8: no overlong forms, no surrogates, max U+10FFFF. */
int utf8_valid(const char* s, size_t n);

/* Chat text: 1..TEXT_MAX_LEN bytes, valid UTF-8, no control chars
 * except tab. */
int text_valid(const char* s);

#define NAME_MIN_LEN 2

/* Names: NAME_MIN_LEN..NAME_MAX_LEN bytes, valid UTF-8, made of ASCII
 * letters, digits, '_', '-', '.' or any non-ASCII character (so names
 * like "Şükrü" work). No spaces or control characters. */
int name_valid(const char* s);

/* Case-insensitive (ASCII) comparison used for duplicate detection. */
int name_equal(const char* a, const char* b);

#endif
