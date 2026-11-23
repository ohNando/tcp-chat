#ifndef NETWORK_H
#define NETWORK_H

#include "utility.h"

int connectToServer(const char*, int);
/* Puts the descriptor into O_NONBLOCK mode. Returns 0 or -1. */
int net_set_nonblocking(int fd);

/* Creates a nonblocking IPv4 listening socket on all interfaces with
 * SO_REUSEADDR. Returns the fd, or -1 (errno set) instead of exiting. */
int net_listen(int port, int backlog);

/* Formats "ip:port" of an IPv4 address into out (thread-safe). */
void net_format_addr(const struct sockaddr_in* addr, char* out, size_t cap);

#endif