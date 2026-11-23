#include "room.h"
#include "validate.h"

#include <stdlib.h>
#include <string.h>

int room_init(room_t* r, size_t max) {
    r->slots = calloc(max, sizeof(*r->slots));
    r->max = max;
    r->count = 0;
    return r->slots ? 0 : -1;
}

void room_free(room_t* r) {
    free(r->slots);
    r->slots = NULL;
    r->max = r->count = 0;
}

int room_add(room_t* r, conn_t* c) {
    for (size_t i = 0; i < r->max; i++) {
        if (!r->slots[i]) {
            r->slots[i] = c;
            r->count++;
            return 0;
        }
    }
    return -1;
}

void room_remove(room_t* r, conn_t* c) {
    for (size_t i = 0; i < r->max; i++) {
        if (r->slots[i] == c) {
            r->slots[i] = NULL;
            r->count--;
            return;
        }
    }
}

conn_t* room_find_name(const room_t* r, const char* name) {
    for (size_t i = 0; i < r->max; i++) {
        conn_t* c = r->slots[i];
        if (c && c->registered && name_equal(c->name, name)) return c;
    }
    return NULL;
}

size_t room_member_count(const room_t* r) {
    size_t n = 0;
    for (size_t i = 0; i < r->max; i++)
        if (r->slots[i] && r->slots[i]->registered) n++;
    return n;
}

void room_send_to(conn_t* c, const message_t* m) {
    if (c->want_close) return;
    if (conn_send(c, m) < 0) c->want_close = 1; /* backlog full */
}

void room_broadcast(room_t* r, const message_t* m, const conn_t* except) {
    for (size_t i = 0; i < r->max; i++) {
        conn_t* c = r->slots[i];
        if (c && c != except && c->registered) room_send_to(c, m);
    }
}

void room_fill_users(const room_t* r, message_t* m) {
    message_init(m, FT_USERS);
    for (size_t i = 0; i < r->max && m->user_count < USERS_MAX; i++) {
        conn_t* c = r->slots[i];
        if (c && c->registered) {
            memcpy(m->users[m->user_count], c->name, sizeof(c->name));
            m->user_count++;
        }
    }
}
