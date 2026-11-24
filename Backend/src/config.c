#include "config.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

int config_path(const char* name, const char* env_override, char* out, size_t cap) {
    const char* env = env_override ? getenv(env_override) : NULL;
    if (env && *env) return snprintf(out, cap, "%s", env) < (int)cap ? 0 : -1;

    const char* xdg = getenv("XDG_CONFIG_HOME");
    if (xdg && *xdg) return snprintf(out, cap, "%s/tcp-chat/%s", xdg, name) < (int)cap ? 0 : -1;

    const char* home = getenv("HOME");
    if (!home || !*home) return -1;
    return snprintf(out, cap, "%s/.config/tcp-chat/%s", home, name) < (int)cap ? 0 : -1;
}

int config_make_parent_dirs(const char* path) {
    char dir[CONFIG_PATH_MAX];
    if (snprintf(dir, sizeof(dir), "%s", path) >= (int)sizeof(dir)) return -1;

    char* slash = strrchr(dir, '/');
    if (!slash || slash == dir) return 0;
    *slash = '\0';

    for (char* p = dir + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(dir, 0700) < 0 && errno != EEXIST) return -1;
        *p = '/';
    }
    if (mkdir(dir, 0700) < 0 && errno != EEXIST) return -1;
    return 0;
}
