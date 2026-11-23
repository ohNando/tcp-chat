#ifndef KNOWN_SERVERS_H
#define KNOWN_SERVERS_H

/*
 * Trust on first use (like SSH's known_hosts).
 *
 * The first time we talk to "host:port" its identity key is saved. Later
 * connections must present the same key; a different key means either the
 * server was reinstalled or someone is in the middle, and we refuse.
 *
 * File: $TCPCHAT_KNOWN_SERVERS, else $XDG_CONFIG_HOME/tcp-chat/known_servers,
 *       else ~/.config/tcp-chat/known_servers
 * Line: "<host>:<port> <64 hex chars of the Ed25519 public key>"
 */

#include <stddef.h>

#include "identity.h"

#define KNOWN_PATH_MAX 4096

/* Fills `out` with the file path. Returns 0 or -1. */
int known_servers_path(char* out, size_t cap);

/* 1: found (key copied to pk), 0: not found, -1: read error. */
int known_servers_lookup(const char* path, const char* hostport,
                         unsigned char pk[IDENTITY_PK_BYTES]);

/* Appends an entry, creating the directory (0700) and file (0600). */
int known_servers_add(const char* path, const char* hostport,
                      const unsigned char pk[IDENTITY_PK_BYTES]);

#endif
