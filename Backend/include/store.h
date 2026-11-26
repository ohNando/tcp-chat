#ifndef STORE_H
#define STORE_H

/*
 * Message history on disk, in SQLite.
 *
 * Every room message gets a stable id (SQLite's rowid), which clients use
 * to ask for older messages and to drop duplicates after a reconnect.
 * Private messages are never stored: they are end-to-end encrypted and the
 * server only relays them.
 *
 * The server is single-threaded, so writes must be short: the database runs
 * in WAL mode with synchronous=NORMAL (an append, fsync only at
 * checkpoints). A crash can lose the last few messages, never corrupt the
 * file.
 */

#include <stddef.h>
#include <stdint.h>

#include "message.h"

/* At most this many messages per history page. */
#define STORE_PAGE_MAX 100

typedef struct sqlite3 sqlite3;
typedef struct sqlite3_stmt sqlite3_stmt;

typedef struct {
    sqlite3* db;
    sqlite3_stmt* insert;
    sqlite3_stmt* page;
    sqlite3_stmt* prune;
    int retention_days;       /* 0 = keep forever */
    int in_tx;                /* a transaction is open (see store_flush) */
} store_t;

typedef struct {
    uint64_t id;
    uint64_t timestamp;
    char name[NAME_MAX_LEN + 1];
    char text[TEXT_MAX_LEN + 1];
} stored_message_t;

/* Opens (creating if needed) the database at `path`; ":memory:" keeps
 * history only while the server runs. Returns 0 or -1 (error logged). */
int store_open(store_t* s, const char* path, int retention_days);
void store_close(store_t* s);

/* Saves one message. Returns its id (> 0), or 0 if it could not be saved
 * (the message is still delivered, just not kept). Messages are grouped
 * into one transaction until store_flush(): one commit per event-loop turn
 * instead of one per message. */
uint64_t store_add(store_t* s, uint64_t timestamp, const char* name, const char* text);

/* Calls `cb` for up to `limit` messages older than `before_id` (0 = the
 * newest), oldest first. Sets *more when even older messages exist.
 * Returns the number of messages, or -1 on error. */
int store_page(store_t* s, uint64_t before_id, int limit,
               void (*cb)(void* ctx, const stored_message_t* m), void* ctx, int* more);

/* Commits the messages added since the last call. */
void store_flush(store_t* s);

/* Deletes messages older than the retention period. Returns how many. */
int store_prune(store_t* s, uint64_t now_ms);

#endif
