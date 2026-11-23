#include "test.h"
#include "known_servers.h"

#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

static char dir[] = "/tmp/tcpchat_known_XXXXXX";
static char path[256];

TEST(lookup_in_missing_file_finds_nothing) {
    unsigned char pk[IDENTITY_PK_BYTES];
    CHECK_EQ_INT(known_servers_lookup(path, "127.0.0.1:8080", pk), 0);
}

TEST(add_then_lookup) {
    unsigned char a[IDENTITY_PK_BYTES], b[IDENTITY_PK_BYTES], got[IDENTITY_PK_BYTES];
    randombytes_buf(a, sizeof(a));
    randombytes_buf(b, sizeof(b));

    CHECK_EQ_INT(known_servers_add(path, "127.0.0.1:8080", a), 0);
    CHECK_EQ_INT(known_servers_add(path, "10.0.0.5:9000", b), 0);

    CHECK_EQ_INT(known_servers_lookup(path, "127.0.0.1:8080", got), 1);
    CHECK_EQ_MEM(got, a, sizeof(a));
    CHECK_EQ_INT(known_servers_lookup(path, "10.0.0.5:9000", got), 1);
    CHECK_EQ_MEM(got, b, sizeof(b));

    /* Prefix of another entry must not match. */
    CHECK_EQ_INT(known_servers_lookup(path, "127.0.0.1:80", got), 0);

    struct stat st;
    CHECK_EQ_INT(stat(path, &st), 0);
    CHECK_EQ_INT(st.st_mode & 0777, 0600);
}

TEST(creates_nested_directories) {
    char nested[300];
    unsigned char pk[IDENTITY_PK_BYTES] = { 1 };
    snprintf(nested, sizeof(nested), "%s/a/b/known_servers", dir);
    CHECK_EQ_INT(known_servers_add(nested, "h:1", pk), 0);
    CHECK_EQ_INT(known_servers_lookup(nested, "h:1", pk), 1);
    unlink(nested);
    char sub[300];
    snprintf(sub, sizeof(sub), "%s/a/b", dir); rmdir(sub);
    snprintf(sub, sizeof(sub), "%s/a", dir); rmdir(sub);
}

TEST(path_prefers_env_override) {
    char out[KNOWN_PATH_MAX];
    setenv("TCPCHAT_KNOWN_SERVERS", "/x/y", 1);
    CHECK_EQ_INT(known_servers_path(out, sizeof(out)), 0);
    CHECK_EQ_STR(out, "/x/y");
    unsetenv("TCPCHAT_KNOWN_SERVERS");

    setenv("XDG_CONFIG_HOME", "/cfg", 1);
    CHECK_EQ_INT(known_servers_path(out, sizeof(out)), 0);
    CHECK_EQ_STR(out, "/cfg/tcp-chat/known_servers");
}

int main(void) {
    if (sodium_init() < 0) return 1;
    if (!mkdtemp(dir)) return 1;
    snprintf(path, sizeof(path), "%s/known_servers", dir);

    RUN(lookup_in_missing_file_finds_nothing);
    RUN(add_then_lookup);
    RUN(creates_nested_directories);
    RUN(path_prefers_env_override);

    unlink(path);
    rmdir(dir);
    return TEST_REPORT();
}
