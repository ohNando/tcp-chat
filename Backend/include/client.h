#ifndef CLIENT_H
#define CLIENT_H

/*
 * Terminal chat client.
 *
 * A single poll() loop watches stdin and the socket, so no threads are
 * needed: whichever is readable gets handled. The client is a small state
 * machine: it first asks for a name until the server sends WELCOME, then
 * switches to chatting.
 */

#define CLIENT_DEFAULT_HOST "127.0.0.1"
#define CLIENT_DEFAULT_PORT 8080
#define CLIENT_LINE_MAX     4096

typedef enum {
    CLIENT_HANDSHAKE,    /* secure channel being set up */
    CLIENT_NAMING,       /* waiting for the user to type a name */
    CLIENT_REGISTERING,  /* NAME sent, waiting for WELCOME or ERR */
    CLIENT_CHATTING,
    CLIENT_QUITTING,
} client_state_t;

#endif
