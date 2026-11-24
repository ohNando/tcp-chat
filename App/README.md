# Desktop app

A native chat client written in C++17 with [Dear ImGui](https://github.com/ocornut/imgui)
on GLFW + OpenGL 3. It reuses the server's C protocol and crypto modules
directly from [`Backend/src`](../Backend/src), so it always speaks exactly
the same wire format.

## Requirements

- Linux (POSIX sockets)
- CMake 3.20+, a C11 and C++17 compiler
- GLFW 3, OpenGL, libsodium
- Internet access on the first configure (Dear ImGui v1.92.9b is fetched
  from GitHub)

```bash
# Fedora
sudo dnf install gcc-c++ cmake pkgconf-pkg-config glfw-devel mesa-libGL-devel libsodium-devel
# Debian / Ubuntu
sudo apt install build-essential cmake pkg-config libglfw3-dev libgl-dev libsodium-dev
```

A system font with Latin Extended glyphs is used so names like `Şükrü`
render correctly (Noto Sans, Liberation Sans or DejaVu Sans; most desktops
have one of them).

## Build and run

```bash
cmake -S . -B build
cmake --build build -j
./build/tcp-chat-desktop
```

Start a server first (`make run-server` in `Backend`), then enter the host,
port (8080) and a name.

## Features

- **Trust prompt:** on the first connection to a server the app shows its
  fingerprint and asks before saving it to `~/.config/tcp-chat/known_servers`
  (shared with the terminal client). If a known server later presents a
  different key, the connection is refused with a warning.
- Chat log with timestamps, a stable color per user and smart auto-scroll
  (it does not jump while you are reading older messages).
- Online users panel, `/users` and `/quit` commands.
- Recent history replayed on join (shown faded) and "is typing…"
  indicators.
- End-to-end encrypted private messages: click a name marked `[dm]` in the
  user list. The safety number is shown above the input box; compare it
  with the other person.
- Reconnects automatically with backoff (1 s, 2 s, 4 s … 15 s) if the
  connection drops.
- No threads: the socket is polled once per frame, and when idle the app
  sleeps until input arrives (waking every 50 ms for the network).

## Command line

```
tcp-chat-desktop [--connect host port name] [--say text] [--trust]
                 [--screenshot out.ppm --wait seconds]
```

| Option | |
|---|---|
| `--connect` | connect on start-up |
| `--say` | send one message once joined |
| `--trust` | accept an unknown server key without asking |
| `--screenshot` | render in a hidden window, save a PPM after `--wait` seconds, exit |

The last three exist for automated checks, for example:

```bash
./build/tcp-chat-desktop --connect 127.0.0.1 8080 demo --trust \
    --say "hello" --screenshot shot.ppm --wait 2
magick shot.ppm shot.png
```

## Tests

`client_smoke` drives the networking layer without a window:

```bash
./build/client_smoke 127.0.0.1 8080
```

It uses a temporary `known_servers` file and checks the trust prompt,
joining, message echo, duplicate names, quitting and key-change detection.
