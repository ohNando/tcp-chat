# Web client

A React (Vite) chat client that runs in the browser and talks to the C
server over WebSocket, with the same end-to-end encrypted protocol as the
terminal and desktop clients (libsodium compiled to WebAssembly).

## Requirements

- Node.js 22 or newer
- A running server from [`Backend`](../Backend)

## Install

```bash
npm install
```

## Use it

**Served by the C server** (recommended):

```bash
npm run build                    # writes dist/
cd ../Backend && make run-server # serves dist/ on port 8081
```

Open <http://localhost:8081>.

**Development with hot reload:**

```bash
cd ../Backend && make run-server # in one terminal
npm run dev                      # in another
```

Open <http://localhost:5173>. Vite proxies `/ws` to the server on port 8081.

## Scripts

| Script | |
|---|---|
| `npm run dev` | Vite dev server |
| `npm run build` | production build in `dist/` |
| `npm run lint` | ESLint |
| `npm test` | unit tests (`node --test`) for the codec and pinning |
| `npm run e2e` | starts `../Backend/bin/server` and chats over TCP and WebSocket |

`npm run e2e` needs the server binary: run `make` in `Backend` first.

## How it works

| File | |
|---|---|
| `src/protocol/frame.js` | binary frame and message codec, mirror of `Backend/src/message.c` |
| `src/protocol/secure.js` | handshake and encrypted record layer, mirror of `Backend/src/secure.c` |
| `src/protocol/pins.js` | remembers the server fingerprint in `localStorage` |
| `src/hooks/useChat.js` | WebSocket connection, reconnect with backoff, chat state |
| `src/App.jsx` | login, chat, online users and the "identity changed" warning |

The server is the single source of truth: your own messages appear when the
server broadcasts them back, so every client shows the same order.

On the first connection the server's fingerprint is saved for that host. If
it ever changes, the app refuses to connect and shows the old and new
fingerprints side by side. Compare them with the one the server prints at
start-up before choosing to trust the new key.

> The page itself is served over plain HTTP. That is fine on `localhost`,
> but over a network someone in the middle could modify the JavaScript
> before it reaches the browser. Serve it over HTTPS (for example behind a
> reverse proxy) for anything beyond local use.
