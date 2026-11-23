#ifndef ROOM_H
#define ROOM_H

/*
 * The chat room: a fixed table of connection slots.
 *
 * Every accepted connection gets a slot, but only `registered` ones
 * (that sent a valid NAME) receive broadcasts and appear in user lists.
 *
 * Sending never blocks; if a member's output backlog is full it is
 * flagged with `want_close` and the event loop drops it afterwards.
 */

#include <stddef.h>

#include "conn.h"
#include "message.h"

typedef struct {
    conn_t** slots;
    size_t max;
    size_t count;
} room_t;

int room_init(room_t* r, size_t max);
void room_free(room_t* r);

/* Returns 0, or -1 when the room is full. */
int room_add(room_t* r, conn_t* c);
void room_remove(room_t* r, conn_t* c);

/* Registered member with this name (case-insensitive), or NULL. */
conn_t* room_find_name(const room_t* r, const char* name);

size_t room_member_count(const room_t* r);

/* Sends to every registered member except `except` (may be NULL). */
void room_broadcast(room_t* r, const message_t* m, const conn_t* except);

/* Sends `m` to one connection, flagging it for close on failure. */
void room_send_to(conn_t* c, const message_t* m);

/* Fills a USERS message with registered member names. */
void room_fill_users(const room_t* r, message_t* m);

#endif
