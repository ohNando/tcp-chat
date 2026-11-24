#include "known_servers.h"
#include "config.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define HEX_LEN (IDENTITY_PK_BYTES * 2)

int known_servers_path(char* out, size_t cap) {
    return config_path("known_servers", "TCPCHAT_KNOWN_SERVERS", out, cap);
}

int known_servers_lookup(const char* path, const char* hostport,
                         unsigned char pk[IDENTITY_PK_BYTES]) {
    FILE* f = fopen(path, "r");
    if (!f) return errno == ENOENT ? 0 : -1;

    char line[512];
    size_t key_len = strlen(hostport);
    int found = 0;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || strncmp(line, hostport, key_len) != 0 || line[key_len] != ' ')
            continue;

        const char* hex = line + key_len + 1;
        size_t bin_len;
        if (sodium_hex2bin(pk, IDENTITY_PK_BYTES, hex, HEX_LEN, NULL, &bin_len, NULL) == 0 &&
            bin_len == IDENTITY_PK_BYTES) {
            found = 1;
            /* Keep reading: the last entry for a host wins. */
        }
    }
    fclose(f);
    return found;
}

int known_servers_add(const char* path, const char* hostport,
                      const unsigned char pk[IDENTITY_PK_BYTES]) {
    char hex[HEX_LEN + 1];
    if (config_make_parent_dirs(path) < 0) return -1;

    int fd = open(path, O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600);
    if (fd < 0) return -1;

    sodium_bin2hex(hex, sizeof(hex), pk, IDENTITY_PK_BYTES);
    char line[512];
    int n = snprintf(line, sizeof(line), "%s %s\n", hostport, hex);
    int rc = (n > 0 && n < (int)sizeof(line) && write(fd, line, (size_t)n) == n) ? 0 : -1;
    close(fd);
    return rc;
}
