#include "dm.h"
#include "bytebuf.h"
#include "config.h"
#include "validate.h"

#include <errno.h>
#include <fcntl.h>
#include <sodium.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAGIC_LEN (sizeof(USER_KEY_FILE_MAGIC) - 1)
#define SEED_LEN  crypto_box_SEEDBYTES
#define FILE_LEN  (MAGIC_LEN + SEED_LEN)
#define SAFETY_CONTEXT "TCHT-safety-v1"

int user_key_path(char* out, size_t cap) {
    return config_path("user.key", "TCPCHAT_USER_KEY", out, cap);
}

int user_key_load_or_create(user_key_t* key, const char* path, int* created) {
    uint8_t buf[FILE_LEN + 1];
    *created = 0;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        ssize_t n = read(fd, buf, sizeof(buf));
        close(fd);
        int ok = n == (ssize_t)FILE_LEN && memcmp(buf, USER_KEY_FILE_MAGIC, MAGIC_LEN) == 0;
        if (ok) crypto_box_seed_keypair(key->pk, key->sk, buf + MAGIC_LEN);
        sodium_memzero(buf, sizeof(buf));
        return ok ? 0 : -1;
    }
    if (errno != ENOENT) return -1;

    if (config_make_parent_dirs(path) < 0) return -1;
    fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) return -1;

    memcpy(buf, USER_KEY_FILE_MAGIC, MAGIC_LEN);
    randombytes_buf(buf + MAGIC_LEN, SEED_LEN);
    crypto_box_seed_keypair(key->pk, key->sk, buf + MAGIC_LEN);
    int rc = write(fd, buf, FILE_LEN) == (ssize_t)FILE_LEN ? 0 : -1;
    sodium_memzero(buf, sizeof(buf));
    close(fd);
    if (rc < 0) {
        unlink(path);
        user_key_wipe(key);
        return -1;
    }
    *created = 1;
    return 0;
}

void user_key_wipe(user_key_t* key) {
    sodium_memzero(key, sizeof(*key));
}

int dm_seal(const user_key_t* me, const uint8_t peer_pk[DM_KEY_BYTES],
            uint64_t sent_at, const char* text,
            uint8_t nonce[DM_NONCE_BYTES], uint8_t box[DM_BOX_MAX], uint16_t* box_len) {
    uint8_t plain[DM_BOX_MAX - crypto_box_MACBYTES];
    bytebuf_t b;

    if (!text_valid(text)) return -1;
    bb_init_writer(&b, plain, sizeof(plain));
    bb_put_u64(&b, sent_at);
    bb_put_str(&b, text);
    if (b.error) return -1;

    randombytes_buf(nonce, DM_NONCE_BYTES);
    if (crypto_box_easy(box, plain, b.pos, nonce, peer_pk, me->sk) != 0) {
        sodium_memzero(plain, sizeof(plain));
        return -1;
    }
    *box_len = (uint16_t)(b.pos + crypto_box_MACBYTES);
    sodium_memzero(plain, sizeof(plain));
    return 0;
}

int dm_open(const user_key_t* me, const uint8_t peer_pk[DM_KEY_BYTES],
            const uint8_t nonce[DM_NONCE_BYTES], const uint8_t* box, size_t box_len,
            uint64_t* sent_at, char text[TEXT_MAX_LEN + 1]) {
    uint8_t plain[DM_BOX_MAX - crypto_box_MACBYTES];
    bytebuf_t b;

    if (box_len < crypto_box_MACBYTES || box_len > DM_BOX_MAX) return -1;
    size_t plain_len = box_len - crypto_box_MACBYTES;
    if (crypto_box_open_easy(plain, box, box_len, nonce, peer_pk, me->sk) != 0) return -1;

    bb_init_reader(&b, plain, plain_len);
    bb_get_u64(&b, sent_at);
    bb_get_str(&b, text, TEXT_MAX_LEN + 1);
    int ok = !b.error && bb_remaining(&b) == 0 && text_valid(text);
    sodium_memzero(plain, sizeof(plain));
    return ok ? 0 : -1;
}

void dm_safety_number(const uint8_t a[DM_KEY_BYTES], const uint8_t b[DM_KEY_BYTES],
                      char out[DM_SAFETY_STR_LEN]) {
    /* Sort the keys so both people compute the same number. */
    const uint8_t* lo = memcmp(a, b, DM_KEY_BYTES) <= 0 ? a : b;
    const uint8_t* hi = lo == a ? b : a;

    crypto_generichash_state st;
    uint8_t hash[32];
    crypto_generichash_init(&st, NULL, 0, sizeof(hash));
    crypto_generichash_update(&st, (const uint8_t*)SAFETY_CONTEXT, sizeof(SAFETY_CONTEXT) - 1);
    crypto_generichash_update(&st, lo, DM_KEY_BYTES);
    crypto_generichash_update(&st, hi, DM_KEY_BYTES);
    crypto_generichash_final(&st, hash, sizeof(hash));

    char* p = out;
    for (int i = 0; i < 8; i++) {
        uint32_t v = ((uint32_t)hash[4 * i] << 24) | ((uint32_t)hash[4 * i + 1] << 16) |
                     ((uint32_t)hash[4 * i + 2] << 8) | hash[4 * i + 3];
        p += sprintf(p, i ? " %05u" : "%05u", (unsigned)(v % 100000u));
    }
}
