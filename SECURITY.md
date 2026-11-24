# Security policy

## Reporting a vulnerability

Please do **not** open a public issue for security problems. Use GitHub's
private vulnerability reporting instead:
**Security → Report a vulnerability** on the repository page.

Include what you found, how to reproduce it, and what an attacker could do
with it. You should get an answer within a week.

This is a hobby and learning project: there is no bug bounty, but every
valid report is credited (unless you prefer otherwise).

## Supported versions

Only the latest commit on `main` is supported.

## Threat model

### In scope

| Threat | Mitigation |
|---|---|
| Eavesdropping on the network | Every record is encrypted with XChaCha20-Poly1305 (`crypto_secretstream`). |
| Modifying, replaying, dropping or reordering records | Poly1305 tags and the stream's internal counter; any such record fails to decrypt and the connection is closed. |
| Impersonating the server (man in the middle) | The server signs each handshake with its Ed25519 identity key; clients pin that key on first use and refuse a different one later. |
| Compromise of the server's long-term key exposing past traffic | Session keys come from ephemeral X25519 keys that are wiped after the handshake (forward secrecy). |
| Malformed input crashing the server | Strict, bounds-checked decoders; limits on record, frame, name and text sizes; tested under valgrind. |
| Slow or idle clients exhausting the server | Per-connection output cap (1 MiB), a 10 s handshake timeout and a member limit. |
| Terminal escape injection | Names and messages with control characters are rejected by the server and sanitized again by the terminal client. |
| HTML / script injection in the web client | React renders all user text as text, never as HTML. |
| The server operator reading private messages | Private messages are end-to-end encrypted with `crypto_box` between the two users' keys; the server only relays ciphertext and never logs DMs. |
| The server substituting a user's DM key | Clients show a safety number per conversation that both people can compare, and warn when a known user's key changes. |

### Out of scope / known limitations

- **Room messages are not end-to-end encrypted.** The server sees every
  room message in plaintext; only private messages are end-to-end encrypted.
- **Private messages have no forward secrecy.** They use long-term keys:
  whoever steals a user's key file can decrypt that user's past DMs if they
  also recorded the traffic.
- **The server distributes DM keys.** Until two people compare their safety
  number, a malicious server could give them its own keys and read their
  DMs. Key changes for a known name are flagged.
- **DM metadata is visible to the server:** who writes to whom and when.
- **A recorded DM can be replayed by the server.** The sender's timestamp
  inside the box shows when it was really written.
- **Keys at rest are not password protected.** `user.key` is a plain file
  (mode 0600); in the browser the key lives in `localStorage`, so browser
  extensions or other scripts on the same origin could read it.
- **Trust on first use.** The very first connection to a server is not
  protected against a man in the middle unless you compare the fingerprint
  with the one the server prints at start-up.
- **No authentication of users.** Names are not reserved or protected.
- **Plain HTTP for the web client.** The JavaScript itself is not protected
  in transit; serve it over HTTPS beyond `localhost`.
- **No `Origin` check** on WebSocket upgrades. There are no cookies or
  sessions, so this is low risk today.
- **No rate limiting** of messages beyond the output buffer cap.

## Cryptography

All primitives come from [libsodium](https://libsodium.org) (and its
WebAssembly build in the browser); nothing is hand-rolled except the
protocol around them. The only hand-written hash, SHA-1, is used solely for
the WebSocket `Sec-WebSocket-Accept` header as RFC 6455 requires, never for
security. See [docs/PROTOCOL.md](docs/PROTOCOL.md) for the exact handshake.
