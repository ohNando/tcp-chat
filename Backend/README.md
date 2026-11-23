# Backend

The chat server and the terminal client, written in C (C11, POSIX).

- One thread, one `epoll` loop, no mutexes.
- Raw TCP clients on one port, HTTP + WebSocket clients on another, all in
  the same room.
- Every connection is encrypted with libsodium after a signed handshake
  (see [docs/PROTOCOL.md](../docs/PROTOCOL.md)).
- Optionally serves the built web client over HTTP.

## Requirements

- Linux (uses `epoll`, `signalfd`, `sendfile`)
- GCC or Clang, GNU Make, `pkg-config`
- libsodium development files
- valgrind (optional, for `make memcheck`)

```bash
# Fedora
sudo dnf install gcc make pkgconf-pkg-config libsodium-devel valgrind
# Debian / Ubuntu
sudo apt install build-essential pkg-config libsodium-dev valgrind
```

## Build

```bash
make            # builds bin/server and bin/client
make test       # unit tests
make memcheck   # unit tests under valgrind
make clean
```

Warnings are errors (`-Wall -Wextra -Werror`).

## Run the server

```bash
make run-server
# or
./bin/server [-p port] [-w ws_port] [-d web_root] [-m max_clients] [-k key_file]
```

| Option | Default | Meaning |
|---|---|---|
| `-p` | `8080` | raw TCP port for the terminal client and desktop app |
| `-w` | `8081` | HTTP / WebSocket port, `0` disables it |
| `-d` | none | directory to serve over HTTP (e.g. `../Web/dist`) |
| `-m` | `64` | maximum number of members, 1–256 |
| `-k` | `server_identity.key` | identity key file, created on first start |

`make run-server` passes `-d ../Web/dist` automatically if the web client
has been built (`npm run build` in `Web`).

On start the server prints its **fingerprint**:

```
[16:23:44] server fingerprint: DDE0:1D04:25DE:0DB4:1C36:E1AA:DE1F:3B63:...
```

Clients show the same fingerprint on their first connection; compare the
two to be sure you are talking to the right server. Keep
`server_identity.key` private (it is created with mode `0600` and is
ignored by git). Deleting it gives the server a new identity, and every
client will then warn that the key changed.

Stop the server with Ctrl+C; it closes all connections and exits cleanly.

## Run the terminal client

```bash
make run-client
# or
./bin/client [host] [port]      # defaults: 127.0.0.1 8080
```

Type a name, then chat. Commands:

| Command | |
|---|---|
| `/users` | list online users |
| `/quit` | leave the chat |
| `/help` | show the commands |

Known server keys are stored in `~/.config/tcp-chat/known_servers`
(override with `TCPCHAT_KNOWN_SERVERS`).

## Layout

```
include/   headers, one per module
src/       server.c, client.c and the shared modules
tests/     unit tests (one binary per test_*.c) and a tiny test framework
```

See [docs/ARCHITECTURE.md](../docs/ARCHITECTURE.md) for how the modules fit
together.
