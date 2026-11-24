# Architecture

## Overview

```
 terminal client ─┐  TCP :8080
 desktop app ─────┤  u32 length + encrypted record
                  ▼
        ┌──────────────────────────────────────┐
        │ server  (one thread, epoll)           │
        │                                       │
        │  listeners ── accept ──▶ conn table   │
        │  signalfd  ── SIGINT/SIGTERM          │
        │                                       │
        │  conn: transport → secure → frames    │
        │  room: names, broadcast, user list    │
        └──────────────────▲───────────────────┘
                           │ HTTP + WebSocket :8081
                     browser (web client)
```

There is exactly one server process with one thread. Every client, whatever
it connects with, becomes a `conn_t` in the same room.

## The event loop (`Backend/src/server.c`)

Each turn of the loop does three things:

1. **`epoll_wait`** returns the ready file descriptors (listening sockets,
   client sockets and a `signalfd`). It wakes up at least once a second to
   expire stalled handshakes.
2. **Handle input.** For every readable client the bytes go into its input
   buffer; complete records are decrypted and decoded, and replies or
   broadcasts are only *queued* in the output buffers of the receivers.
   Nothing is closed here: a connection that should go away is just flagged
   with `want_close`.
3. **`sweep()`** flushes every output buffer with nonblocking `send()`,
   turns `EPOLLOUT` interest on or off depending on what is left, and closes
   flagged connections. Memory is freed only here, so no pointer from the
   current `epoll_wait` batch is ever used after it was freed.

Because nothing ever blocks and only this thread touches the room, there are
no mutexes anywhere in the server.

### Back-pressure

A client that stops reading cannot slow down anyone else. Its messages pile
up in its own output buffer and, once that buffer passes 1 MiB, it is
disconnected. `MSG_NOSIGNAL` and an ignored `SIGPIPE` make a vanished peer a
normal error instead of a crash.

### Heartbeat

`check_timeouts()` runs about once a second: it closes connections that
never finish the handshake (10 s), sends a `PING` to established
connections that have been silent for 40 % of the idle timeout, and drops
those that stay silent for the whole timeout (75 s by default). Without it,
a client whose network vanished would stay in the room forever, because TCP
never notices a peer that just stops answering.

### History and typing

The server keeps the last 50 chat messages in a ring buffer and replays
them, flagged `HISTORY`, to every new member before its `WELCOME`. Clients
draw them faded and skip the ones they already show after a reconnect.
`TYPING` notifications are not stored at all: the server just forwards
them to the other members.

### Private messages

The server stores each member's public DM key (`KEY`) and hands it to the
others (`PEER_KEY`). A `DM` is relayed as an opaque box to the recipient
and echoed to the sender; the server checks only that the recipient exists
and has a key, and does not log it.

### Connection lifecycle

```
accept ──▶ TCP ───────────────────────────┐
                                          ▼
accept ──▶ HTTP ──upgrade──▶ WS ──▶ handshake ──▶ NAME ──▶ member ──▶ close
             │                         │
             └─ GET ─▶ static file     └─ 10 s timeout
```

The server keeps a few spare slots above the member limit, so a client that
arrives when the room is full can still finish the handshake and receive a
proper (encrypted) "room full" error.

## Modules

### Backend (`Backend/src`)

| Module | Role |
|---|---|
| `server.c` | event loop, message handlers, HTTP and WebSocket handling |
| `conn.c` | one connection: input framer, output buffer, `sendfile` streaming |
| `room.c` | member table, name lookup, broadcast, user list |
| `framer.c` | cuts a byte stream into length-prefixed records |
| `bytebuf.c` | bounds-checked big-endian reader/writer |
| `frame.c` / `message.c` | frame header and typed message codec |
| `validate.c` | UTF-8, name and text rules |
| `identity.c` | server Ed25519 key file and fingerprints |
| `handshake.c` | hello messages and the signed transcript |
| `secure.c` | handshake state machine + secretstream record layer |
| `known_servers.c` | client-side trust on first use (servers, and users' DM keys) |
| `config.c` | where client files live (`~/.config/tcp-chat`) |
| `dm.c` | user keys, end-to-end encrypted private messages, safety numbers |
| `http.c` | minimal HTTP request parser |
| `ws.c` / `sha1.c` | WebSocket upgrade and framing (RFC 6455) |
| `client.c` | terminal client (single `poll()` loop on stdin + socket) |

`secure.c` knows nothing about sockets: it consumes record bodies and emits
record bodies through a callback. That is why the same code runs on the
server, in the terminal client, in the desktop app and in the unit tests.

### Web (`Web/src`)

| File | Role |
|---|---|
| `protocol/frame.js` | frame and message codec (mirror of `message.c`) |
| `protocol/secure.js` | client side of the handshake and record layer |
| `protocol/pins.js` | fingerprint pinning in `localStorage` |
| `hooks/useChat.js` | WebSocket, reconnect with backoff, chat state |
| `App.jsx` | login, chat, user list, security warning |
| `chat/`, `theme/`, `components/` | message grouping, themes and backgrounds, settings panel, emoji picker |

### Desktop app (`App/src`)

| File | Role |
|---|---|
| `chat_client.cpp` | nonblocking connection, polled once per frame |
| `app.cpp` | ImGui screens: login, chat, users, DM banner, emoji picker |
| `chat_view.cpp`, `settings_ui.cpp` | message bubbles and the settings panel |
| `ui.cpp`, `theme.cpp`, `wallpaper.cpp` | the web client's design in CSS pixels: fonts, widgets, palettes, backgrounds |
| `emoji_font.cpp` | color emoji from Noto Color Emoji's PNG glyphs |
| `main.cpp` | GLFW window, command line, screenshot mode |

The app compiles the protocol and crypto modules straight from
`Backend/src`, so it cannot drift from the server's wire format.

## Tests

| Command | What it covers |
|---|---|
| `make test` (Backend) | unit tests for every module, including crypto attacks |
| `make memcheck` (Backend) | the same tests under valgrind |
| `tools/bench.sh` (Backend) | load scenarios, see [PERFORMANCE.md](PERFORMANCE.md) |
| `npm test` (Web) | JavaScript codec and pinning |
| `npm run e2e` (Web) | real server + a WebSocket and a TCP client in one room |
| `client_smoke` (App) | the desktop networking layer against a real server |
