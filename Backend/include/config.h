#ifndef CONFIG_H
#define CONFIG_H

/*
 * Per-user files of the clients (known servers, known peers, user key).
 *
 * A file lives in $XDG_CONFIG_HOME/tcp-chat/<name>, falling back to
 * ~/.config/tcp-chat/<name>. Each file can be overridden with an
 * environment variable (used by tests).
 */

#include <stddef.h>

#define CONFIG_PATH_MAX 4096

/* Fills `out` with the path of `name`, or the value of `env_override` if
 * that variable is set. Returns 0 or -1. */
int config_path(const char* name, const char* env_override, char* out, size_t cap);

/* mkdir -p (mode 0700) for the directory that will contain `path`. */
int config_make_parent_dirs(const char* path);

#endif
