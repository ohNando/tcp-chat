# Contributing

Thanks for your interest! This is a learning project, so clear code and
explanations matter as much as features. Small, focused pull requests are
easiest to review. Everyone taking part is expected to follow the
[Code of Conduct](CODE_OF_CONDUCT.md).

## What to work on

[docs/ROADMAP.md](docs/ROADMAP.md) lists open work and ideas. For anything
bigger than a small fix, open an issue first so we can agree on the approach
before you write code.

## Setup

See the README in each folder for dependencies:
[Backend](Backend/README.md), [Web](Web/README.md), [App](App/README.md).

## Before opening a pull request

Run the checks for the parts you touched (CI runs all of them):

```bash
# Backend
cd Backend && make && make test && make memcheck

# Web
cd Web && npm ci && npm run lint && npm test && npm run build && npm run e2e

# Desktop app
cd App && cmake -S . -B build && cmake --build build -j
./build/client_smoke 127.0.0.1 8080     # with a server running
```

## Guidelines

- **Protocol changes touch three places:** `Backend/src` (C),
  `Web/src/protocol` (JavaScript) and [docs/PROTOCOL.md](docs/PROTOCOL.md).
  The desktop app compiles the C modules directly. Add tests on both sides.
- **No new crypto primitives.** Use libsodium; only the protocol around it
  lives in this repository.
- **C code** builds warning-free with `-Wall -Wextra -Werror`, checks every
  length before copying, and never blocks the event loop.
- **Explain the why** in comments when something is not obvious, especially
  in networking code.

## Branches and commits

- Work on a branch off `develop`; pull requests target `develop`.
- Every pull request needs CI to pass and a review from the maintainer
  before it is merged; only the maintainer merges.
- One logical change per commit, message as a single sentence with a
  conventional prefix:

  ```
  feat: Add typing indicator to the web client.
  fix: Close connections that send oversized records.
  docs: Explain the handshake transcript.
  test: Cover fragmented websocket messages.
  refactor: Extract HTTP handling from server.c.
  chore: Update dependencies.
  ```

## Reporting bugs and ideas

Use the issue templates. For security problems, follow
[SECURITY.md](SECURITY.md) instead of opening a public issue.
