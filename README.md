# TCP Chat

[![CI](https://github.com/ohNando/tcp-chat/actions/workflows/ci.yml/badge.svg)](https://github.com/ohNando/tcp-chat/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

An encrypted chat built from scratch to learn how networking really works:
a single-threaded `epoll` server in C, a binary protocol, a hand-written
WebSocket implementation, and three clients that all meet in the same room:
a terminal client, a React web client and a native Dear ImGui desktop app.
The web and desktop clients look like a modern messaging app, and look the
same as each other.

| Web client | Desktop app |
|---|---|
| ![Web client](docs/images/web.png) | ![Desktop app](docs/images/desktop.png) |

![Themes and backgrounds](docs/images/themes.png)

## Features

### Chat

- **Three clients, one room.** Terminal, browser and desktop users see each
  other, with join/leave notices, a live user list, recent history for
  newcomers and "is typing…" indicators.
- **Messaging-app look.** Bubbles grouped by sender, avatars, day
  separators, emoji (with a picker) and an unread count in the title, plus
  optional notifications for private messages and mentions.
- **Make it yours.** Six themes (or follow the system's light/dark mode),
  your own accent and bubble colors, built-in backgrounds or your own picture
  with dim and blur, three text sizes and a compact mode. Settings are kept
  between sessions.

  ![Settings panel](docs/images/settings.png)

- **End-to-end encrypted private messages.** Click a 🔒 name to write
  privately. DMs are sealed with each user's own key (`crypto_box`); the
  server only relays ciphertext, and a safety number lets two people check
  that nobody swapped their keys.
- **Resilient.** A heartbeat drops dead peers, and the web and desktop
  clients reconnect on their own.

### Under the hood

- **One thread, no locks.** The server is a single `epoll` event loop with
  nonblocking sockets, per-connection output buffers and `signalfd` for
  clean shutdown. A slow or dead client can never stall anyone else.
- **Own binary protocol.** Length-prefixed records and a 12-byte frame
  header, decoded strictly and covered by unit tests
  ([docs/PROTOCOL.md](docs/PROTOCOL.md)).
- **Encrypted connections.** X25519 key exchange per connection (forward
  secrecy), an Ed25519-signed handshake, and XChaCha20-Poly1305 records via
  libsodium. Tampered, replayed or reordered records are rejected.
- **Server identity pinning.** Clients remember the server's key on first
  use (quietly; it is shown under Settings → Connection) and refuse to
  connect if it ever changes.
- **WebSocket from scratch.** RFC 6455 upgrade, framing, fragmentation,
  ping/pong and close, plus static file serving with `sendfile()`.
- **Measured, not guessed.** A load generator reports latency percentiles
  and the server's CPU split; see [docs/PERFORMANCE.md](docs/PERFORMANCE.md)
  for how a 34 ms Nagle stall became 0.5 ms.
- **One look, two toolkits.** The desktop app draws the web client's design
  with Dear ImGui: the same fonts (Noto Sans), sizes, colors and color emoji.

## Architecture

```
 terminal client ─┐  TCP :8080
 desktop app ─────┤
                  ▼
        ┌──────────────────────────────┐
        │ server: one thread, epoll     │
        │ conn → secure channel → frame │
        │ room: names, broadcast        │
        └──────────────▲───────────────┘
                       │ HTTP + WebSocket :8081
                 browser (web client)
```

More in [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Quick start

Requirements: Linux, GCC, Make, libsodium, Node.js 22 (web client), CMake
and GLFW (desktop app). Per-component details are in each README.

```bash
# 1. Server and terminal client
cd Backend
make
make run-server

# 2. Terminal client (another terminal)
cd Backend && make run-client

# 3. Web client
cd Web && npm install && npm run build
# restart `make run-server`, then open http://localhost:8081

# 4. Desktop app
cd App && cmake -S . -B build && cmake --build build -j
./build/tcp-chat-desktop   # server 127.0.0.1, port 8080, then pick a name
```

Settings (the ⚙️ button) change the look of the web and desktop clients.

## Project layout

| Folder | |
|---|---|
| [`Backend/`](Backend) | C server, terminal client, shared protocol and crypto modules, unit tests |
| [`Web/`](Web) | React + Vite client, JavaScript protocol implementation, end-to-end test |
| [`App/`](App) | C++ Dear ImGui desktop client with the web client's design |
| [`docs/`](docs) | protocol, architecture, performance notes and the roadmap |

## Security

What the encryption protects, and what it does not:

- Traffic between each client and the server is encrypted and
  authenticated; someone on the network can neither read nor modify it.
- Clients verify the server's identity key (pinned on first use), so a man
  in the middle is detected after the first connection. To also protect the
  first one, compare the fingerprint under Settings → Connection with the
  one the server prints when it starts.
- **Room messages are not end-to-end encrypted.** The server decrypts every
  room message and re-encrypts it for each recipient, so whoever runs the
  server can read the room. **Private messages are end-to-end encrypted**:
  the server only relays them and cannot read them.
- The web client is served over plain HTTP. That is fine on `localhost`;
  over a network, put it behind HTTPS.
- There are no user accounts or passwords: names are first come, first
  served.

See [SECURITY.md](SECURITY.md) for the threat model and how to report a
vulnerability.

## Background

This started as a pre-AI learning project: a thread-per-client chat server
in C to understand sockets and TCP. It has since been rebuilt around an
event loop, a proper framed protocol, encryption and multiple clients,
keeping the goal of learning how each layer works rather than hiding it
behind libraries.

## Contributing

Ideas and open work are listed in [docs/ROADMAP.md](docs/ROADMAP.md); help is
welcome. See [CONTRIBUTING.md](CONTRIBUTING.md) for how to send changes.

## License

[MIT](LICENSE)
