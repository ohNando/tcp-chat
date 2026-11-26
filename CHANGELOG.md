# Changelog

Notable changes per version. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and versions follow
[Semantic Versioning](https://semver.org/) (0.x: the protocol may still
change between minor versions). Planned work is in
[docs/ROADMAP.md](docs/ROADMAP.md).

## [Unreleased]

### Added

- Code of conduct, changelog and monthly Dependabot updates.

## [0.4.0] - 2026-11-26

The messaging-app look, on the web and on the desktop.

### Added

- Chat bubbles grouped by sender, with avatars and day separators.
- Six themes plus "Match system", custom accent and bubble colors.
- Chat backgrounds: built-in patterns and gradients, or your own picture
  with dim and blur.
- Text size (S/M/L) and a compact mode.
- An emoji picker, and color emoji in the desktop app.
- Unread count in the tab and window title, plus optional notifications for
  private messages and mentions.
- A settings panel on web and desktop. Settings are kept in `localStorage`
  on the web and in `~/.config/tcp-chat/desktop.ini` on the desktop.
- Desktop: `--theme`, `--wallpaper`, `--settings`, `--emoji`, `--dm` and
  `--size` options for screenshots.
- A roadmap for contributors ([docs/ROADMAP.md](docs/ROADMAP.md)).

### Changed

- The desktop app now matches the web client's fonts, sizes and layout. It
  uses Noto Sans and Noto Color Emoji, fetched at build time.
- The desktop app trusts a new server on first use, like the web client,
  instead of asking. A changed server key is still refused.
- The server fingerprint moved from the login screen and chat to
  Settings → Connection.

## [0.3.0] - 2026-11-24

### Added

- End-to-end encrypted private messages with safety numbers, in all three
  clients.
- Recent history (last 50 messages) replayed to people who join.
- "Is typing…" indicators.
- A heartbeat that drops unresponsive connections (`-t` idle timeout).
- Brotli and gzip copies of the web build, with long-term caching.
- A load generator and [performance notes](docs/PERFORMANCE.md).

### Fixed

- Latency of 34 ms per message caused by Nagle's algorithm (`TCP_NODELAY`).

## [0.2.0] - 2026-11-23

A rewrite of the original project.

### Added

- A binary, length-prefixed protocol ([docs/PROTOCOL.md](docs/PROTOCOL.md)).
- Encryption between each client and the server with libsodium:
  - X25519 key exchange
  - Ed25519-signed handshake
  - XChaCha20-Poly1305 records
  - server identity pinning
- A hand-written WebSocket server (RFC 6455) and static file serving for the
  web client.
- An encrypted React web client and a native Dear ImGui desktop app.
- Unit tests, an end-to-end test, a browser test and GitHub Actions CI.

### Changed

- The thread-per-client server became a single-threaded `epoll` event loop.

### Removed

- libwebsockets and the Electron wrapper.

## [0.1.0] - 2025-10-30

The original learning project: a thread-per-client TCP chat server and
client in C, with an early React front end through libwebsockets.

[Unreleased]: https://github.com/ohNando/tcp-chat/compare/v0.4.0...HEAD
[0.4.0]: https://github.com/ohNando/tcp-chat/releases/tag/v0.4.0
[0.3.0]: https://github.com/ohNando/tcp-chat/releases/tag/v0.3.0
[0.2.0]: https://github.com/ohNando/tcp-chat/releases/tag/v0.2.0
[0.1.0]: https://github.com/ohNando/tcp-chat/releases/tag/v0.1.0
