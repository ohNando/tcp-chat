#include "identity.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAGIC_LEN (sizeof(IDENTITY_FILE_MAGIC) - 1)
#define SEED_LEN  crypto_sign_SEEDBYTES
#define FILE_LEN  (MAGIC_LEN + SEED_LEN)

static int write_full(int fd, const unsigned char* buf, size_t n) {
    size_t done = 0;
    while (done < n) {
        ssize_t w = write(fd, buf + done, n - done);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return -1;
        done += (size_t)w;
    }
    return 0;
}

static int load(identity_t* id, const char* path) {
    unsigned char buf[FILE_LEN + 1];
    struct stat st;
    int rc = -1;

    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return -1;

    if (fstat(fd, &st) == 0 && (st.st_mode & 077))
        fprintf(stderr, "warning: %s is readable by other users (chmod 600 it)\n", path);

    /* Read one extra byte to detect files that are too long. */
    ssize_t n = read(fd, buf, sizeof(buf));
    if (n == (ssize_t)FILE_LEN && memcmp(buf, IDENTITY_FILE_MAGIC, MAGIC_LEN) == 0) {
        crypto_sign_seed_keypair(id->pk, id->sk, buf + MAGIC_LEN);
        rc = 0;
    } else {
        fprintf(stderr, "error: %s is not a valid identity key file\n", path);
        errno = EINVAL;
    }

    sodium_memzero(buf, sizeof(buf));
    close(fd);
    return rc;
}

static int create(identity_t* id, const char* path) {
    unsigned char buf[FILE_LEN];

    /* O_EXCL: never clobber an existing key. */
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        fprintf(stderr, "error: cannot create %s: %s\n", path, strerror(errno));
        return -1;
    }

    memcpy(buf, IDENTITY_FILE_MAGIC, MAGIC_LEN);
    randombytes_buf(buf + MAGIC_LEN, SEED_LEN);
    crypto_sign_seed_keypair(id->pk, id->sk, buf + MAGIC_LEN);

    int rc = write_full(fd, buf, sizeof(buf));
    if (rc == 0) rc = fsync(fd);
    sodium_memzero(buf, sizeof(buf));
    close(fd);

    if (rc < 0) {
        fprintf(stderr, "error: cannot write %s: %s\n", path, strerror(errno));
        unlink(path);
        identity_wipe(id);
    }
    return rc;
}

int identity_load_or_create(identity_t* id, const char* path, int* created) {
    *created = 0;
    if (access(path, F_OK) == 0) return load(id, path);
    if (errno != ENOENT) {
        fprintf(stderr, "error: cannot access %s: %s\n", path, strerror(errno));
        return -1;
    }
    if (create(id, path) < 0) return -1;
    *created = 1;
    return 0;
}

void identity_wipe(identity_t* id) {
    sodium_memzero(id, sizeof(*id));
}

void identity_fingerprint(const unsigned char pk[IDENTITY_PK_BYTES],
                          char out[FINGERPRINT_STR_LEN]) {
    static const char hex[] = "0123456789ABCDEF";
    unsigned char hash[32];
    crypto_generichash(hash, sizeof(hash), pk, IDENTITY_PK_BYTES, NULL, 0);

    char* p = out;
    for (size_t i = 0; i < sizeof(hash); i++) {
        *p++ = hex[hash[i] >> 4];
        *p++ = hex[hash[i] & 0x0F];
        if (i % 2 == 1) *p++ = (i == sizeof(hash) - 1) ? '\0' : ':';
    }
}
