#include "test.h"
#include "identity.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static char path[] = "/tmp/tcpchat_identity_XXXXXX";

TEST(creates_then_reloads_same_key) {
    identity_t a, b;
    int created;

    CHECK_EQ_INT(identity_load_or_create(&a, path, &created), 0);
    CHECK_EQ_INT(created, 1);

    struct stat st;
    CHECK_EQ_INT(stat(path, &st), 0);
    CHECK_EQ_INT(st.st_mode & 0777, 0600);
    CHECK_EQ_INT(st.st_size, 40);

    CHECK_EQ_INT(identity_load_or_create(&b, path, &created), 0);
    CHECK_EQ_INT(created, 0);
    CHECK_EQ_MEM(a.pk, b.pk, sizeof(a.pk));
    CHECK_EQ_MEM(a.sk, b.sk, sizeof(a.sk));
}

TEST(key_signs_and_verifies) {
    identity_t id;
    int created;
    identity_load_or_create(&id, path, &created);

    unsigned char sig[crypto_sign_BYTES];
    const unsigned char msg[] = "handshake transcript";
    crypto_sign_detached(sig, NULL, msg, sizeof(msg), id.sk);
    CHECK_EQ_INT(crypto_sign_verify_detached(sig, msg, sizeof(msg), id.pk), 0);
}

TEST(rejects_corrupt_file) {
    FILE* f = fopen(path, "wb");
    fputs("not a key", f);
    fclose(f);

    identity_t id;
    int created;
    CHECK_EQ_INT(identity_load_or_create(&id, path, &created), -1);
}

TEST(fingerprint_format) {
    unsigned char pk[IDENTITY_PK_BYTES] = { 0 };
    char fp[FINGERPRINT_STR_LEN];
    identity_fingerprint(pk, fp);

    CHECK_EQ_INT(strlen(fp), FINGERPRINT_STR_LEN - 1);
    CHECK_EQ_INT(fp[4], ':');
    CHECK(fp[FINGERPRINT_STR_LEN - 2] != ':');

    pk[0] = 1;
    char fp2[FINGERPRINT_STR_LEN];
    identity_fingerprint(pk, fp2);
    CHECK(strcmp(fp, fp2) != 0);
}

int main(void) {
    if (sodium_init() < 0) return 1;
    int fd = mkstemp(path);
    close(fd);
    unlink(path); /* we only wanted a unique name */

    RUN(creates_then_reloads_same_key);
    RUN(key_signs_and_verifies);
    unlink(path);
    RUN(rejects_corrupt_file);
    RUN(fingerprint_format);

    unlink(path);
    return TEST_REPORT();
}
