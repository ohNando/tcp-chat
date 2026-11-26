#ifndef SERVER_H
#define SERVER_H

/*
 * Single-threaded chat server built around epoll.
 *
 * One loop turn:
 *   1. epoll_wait() reports ready sockets.
 *   2. Readable sockets are drained into their framer, complete records
 *      are decoded and handled. Replies/broadcasts are only *queued*.
 *      Connections are never closed here, only flagged with want_close.
 *   3. sweep() flushes queued output, adjusts EPOLLOUT interest and
 *      closes flagged connections. Freeing memory only here guarantees
 *      no pointer from the current epoll batch is used after free.
 *
 * No threads means no mutexes: the room is only ever touched by the loop.
 */

#include "conn.h"
#include "room.h"

#define SERVER_DEFAULT_PORT        8080
#define SERVER_DEFAULT_WS_PORT     8081
#define SERVER_DEFAULT_MAX_CLIENTS 64
#define SERVER_MAX_EVENTS          64
#define SERVER_LISTEN_BACKLOG      128
#define SERVER_SPARE_SLOTS         8
#define SERVER_HANDSHAKE_TIMEOUT_MS 10000
/* Heartbeat: a connection silent for 40% of the idle timeout gets a PING;
 * one silent for the whole timeout is considered dead (pulled cable,
 * suspended laptop: TCP alone would keep it "open" forever). */
#define SERVER_DEFAULT_IDLE_TIMEOUT_S 75
/* Recent CHAT messages replayed to people who join (FLAG_HISTORY); older
 * ones are fetched page by page with HISTORY_REQ. */
#define SERVER_HISTORY_MAX 50
#define SERVER_DEFAULT_HISTORY_DB "chat.db"
/* Flood protection: frames per second per connection, and the burst a
 * connection may send at once (SERVER_BURST_FACTOR x rate, at least
 * SERVER_MIN_BURST). A connection that keeps sending while limited is
 * closed once it has had a whole burst dropped. */
#define SERVER_DEFAULT_RATE   5
#define SERVER_BURST_FACTOR   4
#define SERVER_MIN_BURST      10

#endif
