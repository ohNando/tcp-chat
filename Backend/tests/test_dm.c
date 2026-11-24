#include "test.h"
#include "dm.h"

#include <sodium.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static user_key_t from_seed(uint8_t fill) {
    user_key_t k;
    uint8_t seed[crypto_box_SEEDBYTES];
    memset(seed, fill, sizeof(seed));
    crypto_box_seed_keypair(k.pk, k.sk, seed);
    return k;
}

TEST(seal_then_open_both_ways) {
    user_key_t ali = from_seed(1), veli = from_seed(2);
    uint8_t nonce[DM_NONCE_BYTES], box[DM_BOX_MAX];
    uint16_t len;
    uint64_t at;
    char text[TEXT_MAX_LEN + 1];

    CHECK_EQ_INT(dm_seal(&ali, veli.pk, 1700000000000ULL, "gizli: şifreli DM 🔐", nonce, box, &len), 0);
    /* recipient opens with the sender's public key */
    CHECK_EQ_INT(dm_open(&veli, ali.pk, nonce, box, len, &at, text), 0);
    CHECK_EQ_STR(text, "gizli: şifreli DM 🔐");
    CHECK(at == 1700000000000ULL);
    /* the sender can open its own echo with the recipient's public key */
    CHECK_EQ_INT(dm_open(&ali, veli.pk, nonce, box, len, &at, text), 0);
    CHECK_EQ_STR(text, "gizli: şifreli DM 🔐");
}

TEST(third_party_and_tampering_fail) {
    user_key_t ali = from_seed(1), veli = from_seed(2), server = from_seed(3);
    uint8_t nonce[DM_NONCE_BYTES], box[DM_BOX_MAX];
    uint16_t len;
    uint64_t at;
    char text[TEXT_MAX_LEN + 1];

    dm_seal(&ali, veli.pk, 1, "selam", nonce, box, &len);
    CHECK_EQ_INT(dm_open(&server, ali.pk, nonce, box, len, &at, text), -1);
    CHECK_EQ_INT(dm_open(&server, veli.pk, nonce, box, len, &at, text), -1);

    box[3] ^= 1;
    CHECK_EQ_INT(dm_open(&veli, ali.pk, nonce, box, len, &at, text), -1);
}

TEST(seal_rejects_bad_text) {
    user_key_t ali = from_seed(1), veli = from_seed(2);
    uint8_t nonce[DM_NONCE_BYTES], box[DM_BOX_MAX];
    uint16_t len;
    CHECK_EQ_INT(dm_seal(&ali, veli.pk, 1, "", nonce, box, &len), -1);
    CHECK_EQ_INT(dm_seal(&ali, veli.pk, 1, "\x1b[2J", nonce, box, &len), -1);
}

/* Shared with Web/src/protocol/dm.test.js so the two implementations are
 * checked against each other, not just against themselves. */
TEST(fixed_vector_matches_javascript) {
    user_key_t ali = from_seed(1), veli = from_seed(2);
    uint8_t nonce[DM_NONCE_BYTES];
    memset(nonce, 3, sizeof(nonce));

    /* the plaintext dm_seal would produce: u64 sent_at | str text */
    const char* text = "merhaba";
    uint8_t plain[8 + 2 + 7] = { 0, 0, 1, 0x8B, 0xCF, 0xE5, 0x68, 0x00, 0, 7 };
    memcpy(plain + 10, text, 7);
    uint8_t box[sizeof(plain) + crypto_box_MACBYTES];
    CHECK_EQ_INT(crypto_box_easy(box, plain, sizeof(plain), nonce, veli.pk, ali.sk), 0);

    /* ...and it is exactly the format dm_open expects. */
    uint64_t at;
    char out[TEXT_MAX_LEN + 1];
    CHECK_EQ_INT(dm_open(&veli, ali.pk, nonce, box, sizeof(box), &at, out), 0);
    CHECK_EQ_STR(out, "merhaba");
    CHECK(at == 1700000000000ULL);

    char hex[2 * sizeof(box) + 1];
    sodium_bin2hex(hex, sizeof(hex), box, sizeof(box));
    CHECK_EQ_STR(hex, "bff8aa72b8e0d622b610daa242a21a2d1478922eb6903be26ca241961968ec6116");

    char safety[DM_SAFETY_STR_LEN];
    dm_safety_number(ali.pk, veli.pk, safety);
    char safety2[DM_SAFETY_STR_LEN];
    dm_safety_number(veli.pk, ali.pk, safety2);
    CHECK_EQ_STR(safety, safety2);
    CHECK_EQ_INT(strlen(safety), DM_SAFETY_STR_LEN - 1);
    CHECK_EQ_STR(safety, "32479 21887 28548 40510 95895 94034 71636 85843");
}

TEST(user_key_is_created_once_and_reloaded) {
    char dir[] = "/tmp/tcpchat_userkey_XXXXXX";
    if (!mkdtemp(dir)) return;
    char path[256];
    snprintf(path, sizeof(path), "%s/sub/user.key", dir);

    user_key_t a, b;
    int created;
    CHECK_EQ_INT(user_key_load_or_create(&a, path, &created), 0);
    CHECK_EQ_INT(created, 1);
    struct stat st;
    CHECK_EQ_INT(stat(path, &st), 0);
    CHECK_EQ_INT(st.st_mode & 0777, 0600);

    CHECK_EQ_INT(user_key_load_or_create(&b, path, &created), 0);
    CHECK_EQ_INT(created, 0);
    CHECK_EQ_MEM(a.pk, b.pk, DM_KEY_BYTES);

    unlink(path);
    char sub[256];
    snprintf(sub, sizeof(sub), "%s/sub", dir);
    rmdir(sub);
    rmdir(dir);
}

int main(void) {
    if (sodium_init() < 0) return 1;
    RUN(seal_then_open_both_ways);
    RUN(third_party_and_tampering_fail);
    RUN(seal_rejects_bad_text);
    RUN(fixed_vector_matches_javascript);
    RUN(user_key_is_created_once_and_reloaded);
    return TEST_REPORT();
}
