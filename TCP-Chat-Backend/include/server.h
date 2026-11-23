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
#define SERVER_DEFAULT_MAX_CLIENTS 64
#define SERVER_MAX_EVENTS          64
#define SERVER_LISTEN_BACKLOG      128
#define SERVER_SPARE_SLOTS         8

#endif
