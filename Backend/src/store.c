#include "store.h"
#include "utility.h"

#include <sqlite3.h>
#include <stdio.h>
#include <string.h>

static int fail(store_t* s, const char* what) {
    log_info("history store: %s: %s", what, s->db ? sqlite3_errmsg(s->db) : "out of memory");
    return -1;
}

int store_open(store_t* s, const char* path, int retention_days) {
    memset(s, 0, sizeof(*s));
    s->retention_days = retention_days;

    if (sqlite3_open(path, &s->db) != SQLITE_OK) {
        fail(s, "cannot open");
        store_close(s);
        return -1;
    }
    /* Another process holding a lock should not stall the event loop. */
    sqlite3_busy_timeout(s->db, 100);

    const char* setup =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "CREATE TABLE IF NOT EXISTS messages ("
        "  id   INTEGER PRIMARY KEY,"
        "  ts   INTEGER NOT NULL,"
        "  name TEXT NOT NULL,"
        "  text TEXT NOT NULL"
        ");"
        "CREATE INDEX IF NOT EXISTS messages_ts ON messages(ts);";
    if (sqlite3_exec(s->db, setup, NULL, NULL, NULL) != SQLITE_OK) {
        fail(s, "cannot create the table");
        store_close(s);
        return -1;
    }

    /* Prepared once: values are bound, never pasted into SQL. */
    if (sqlite3_prepare_v2(s->db, "INSERT INTO messages(ts, name, text) VALUES (?, ?, ?)", -1,
                           &s->insert, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(s->db,
                           "SELECT id, ts, name, text FROM messages WHERE id < ? "
                           "ORDER BY id DESC LIMIT ?",
                           -1, &s->page, NULL) != SQLITE_OK ||
        sqlite3_prepare_v2(s->db, "DELETE FROM messages WHERE ts < ?", -1, &s->prune, NULL) != SQLITE_OK) {
        fail(s, "cannot prepare statements");
        store_close(s);
        return -1;
    }
    return 0;
}

void store_close(store_t* s) {
    store_flush(s);
    sqlite3_finalize(s->insert);
    sqlite3_finalize(s->page);
    sqlite3_finalize(s->prune);
    sqlite3_close(s->db);
    memset(s, 0, sizeof(*s));
}

uint64_t store_add(store_t* s, uint64_t timestamp, const char* name, const char* text) {
    if (!s->db) return 0;
    if (!s->in_tx) {
        if (sqlite3_exec(s->db, "BEGIN", NULL, NULL, NULL) != SQLITE_OK) {
            fail(s, "cannot begin a transaction");
            return 0;
        }
        s->in_tx = 1;
    }
    sqlite3_reset(s->insert);
    sqlite3_bind_int64(s->insert, 1, (sqlite3_int64)timestamp);
    sqlite3_bind_text(s->insert, 2, name, -1, SQLITE_STATIC);
    sqlite3_bind_text(s->insert, 3, text, -1, SQLITE_STATIC);
    int rc = sqlite3_step(s->insert);
    sqlite3_clear_bindings(s->insert);
    if (rc != SQLITE_DONE) {
        fail(s, "cannot save a message");
        return 0;
    }
    return (uint64_t)sqlite3_last_insert_rowid(s->db);
}

int store_page(store_t* s, uint64_t before_id, int limit,
               void (*cb)(void* ctx, const stored_message_t* m), void* ctx, int* more) {
    *more = 0;
    if (!s->db) return 0;
    if (limit < 1) limit = 1;
    if (limit > STORE_PAGE_MAX) limit = STORE_PAGE_MAX;

    /* Newest first from SQL, one extra row to learn whether more exist;
     * then handed out oldest first so clients can simply append. */
    static stored_message_t rows[STORE_PAGE_MAX];
    sqlite3_reset(s->page);
    sqlite3_bind_int64(s->page, 1, before_id ? (sqlite3_int64)before_id : INT64_MAX);
    sqlite3_bind_int(s->page, 2, limit + 1);

    int n = 0, rc;
    while ((rc = sqlite3_step(s->page)) == SQLITE_ROW) {
        if (n == limit) {
            *more = 1;
            break;
        }
        stored_message_t* m = &rows[n++];
        m->id = (uint64_t)sqlite3_column_int64(s->page, 0);
        m->timestamp = (uint64_t)sqlite3_column_int64(s->page, 1);
        const char* name = (const char*)sqlite3_column_text(s->page, 2);
        const char* text = (const char*)sqlite3_column_text(s->page, 3);
        snprintf(m->name, sizeof(m->name), "%s", name ? name : "");
        snprintf(m->text, sizeof(m->text), "%s", text ? text : "");
    }
    sqlite3_reset(s->page);
    if (rc != SQLITE_ROW && rc != SQLITE_DONE) return fail(s, "cannot read history");

    for (int i = n - 1; i >= 0; i--) cb(ctx, &rows[i]);
    return n;
}

void store_flush(store_t* s) {
    if (!s->db || !s->in_tx) return;
    s->in_tx = 0;
    if (sqlite3_exec(s->db, "COMMIT", NULL, NULL, NULL) != SQLITE_OK) {
        fail(s, "cannot commit messages");
        sqlite3_exec(s->db, "ROLLBACK", NULL, NULL, NULL);
    }
}

int store_prune(store_t* s, uint64_t now_ms) {
    store_flush(s);
    if (!s->db || s->retention_days <= 0) return 0;
    uint64_t cutoff = now_ms - (uint64_t)s->retention_days * 24 * 3600 * 1000;
    sqlite3_reset(s->prune);
    sqlite3_bind_int64(s->prune, 1, (sqlite3_int64)cutoff);
    if (sqlite3_step(s->prune) != SQLITE_DONE) return fail(s, "cannot delete old messages");
    return sqlite3_changes(s->db);
}
