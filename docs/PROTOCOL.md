# TCP Chat protocol

This document describes everything that travels between a client and the
server. The reference implementation is in `Backend/src` (`frame.c`,
`message.c`, `handshake.c`, `secure.c`); the JavaScript client mirrors it in
`Web/src/protocol`.

All integers are **big-endian** (network byte order). Strings are encoded as
`u16 length` followed by that many bytes of UTF-8, without a terminating NUL.

## Layers

```
┌───────────────────────────────────────────────┐
│ message     NAME, MSG, CHAT, JOIN, ...        │  typed payload
├───────────────────────────────────────────────┤
│ frame       12-byte header + payload          │  application framing
├───────────────────────────────────────────────┤
│ record      encrypted with secretstream       │  confidentiality + integrity
├───────────────────────────────────────────────┤
│ transport   TCP: u32 length prefix            │  message boundaries
│             WebSocket: one binary message     │
└───────────────────────────────────────────────┘
```

1. **Transport** delimits records.
   - Raw TCP (port 8080 by default) is a byte stream, so every record is
     prefixed with its length: `u32 length | body`.
   - WebSocket (port 8081 by default, any path) already has message
     boundaries: every record is exactly one **binary** message. Text frames
     are rejected with close code 1003.
2. **Records** carry the handshake first, then ciphertext (see below).
3. **Frames** are what is inside a decrypted record.

Maximum record body: 65 612 bytes (a full frame plus room for the
encryption overhead). Larger records close the connection.

## Handshake

Before any frame can be sent, the client and server set up a secure channel.
All four handshake messages are plain records.

```
Client                                           Server
  │  CLIENT_HELLO  "TCHT" | ver | client_eph_pk[32] │
  │ ───────────────────────────────────────────────▶│
  │                                                 │
  │  SERVER_HELLO  "TCHT" | ver | server_eph_pk[32]  │
  │                | server_id_pk[32] | sig[64]      │
  │ ◀───────────────────────────────────────────────│
  │  server stream header [24]                      │
  │ ◀───────────────────────────────────────────────│
  │                                                 │
  │  client stream header [24]                      │
  │ ───────────────────────────────────────────────▶│
  │                                                 │
  │  encrypted records in both directions           │
```

| Field | Size | Meaning |
|---|---|---|
| `"TCHT"` | 4 | magic |
| `ver` | 1 | protocol version, currently `1` |
| `*_eph_pk` | 32 | fresh X25519 public key for this connection (`crypto_kx`) |
| `server_id_pk` | 32 | the server's long-term Ed25519 identity key |
| `sig` | 64 | Ed25519 signature by the identity key over the transcript |

The **transcript** that the server signs is:

```
"TCHT-handshake-v1" | client_eph_pk | server_eph_pk | server_id_pk
```

The context string stops the signature from being valid for anything else,
and covering both ephemeral keys binds it to this one connection: an old
`SERVER_HELLO` cannot be replayed and keys cannot be swapped in transit.

The client then:

1. verifies the signature; a bad signature closes the connection;
2. checks the identity key against its pinned keys (trust on first use,
   see [Trust](#trust));
3. derives one key per direction with
   `crypto_kx_client_session_keys(client_eph, server_eph)`; the server uses
   `crypto_kx_server_session_keys`. Ephemeral secret keys are wiped right
   away, which gives **forward secrecy**;
4. starts `crypto_secretstream_xchacha20poly1305` in each direction and
   sends its stream header.

A connection that does not finish the handshake within 10 seconds is closed.

## Encrypted records

After the handshake every record body is the output of
`crypto_secretstream_xchacha20poly1305_push`: XChaCha20 encryption plus a
Poly1305 tag (17 bytes of overhead). The stream has an internal counter, so a
record that is modified, replayed, dropped or reordered fails to decrypt and
the connection is closed.

The client marks its last record (the `QUIT` frame) with `TAG_FINAL`; the
server closes the connection after such a record.

## Frame header

```
 0       1       2       3       4               8               12
 ┌───────┬───────┬───────┬───────┬───────────────┬───────────────┬──────────
 │  ver  │ type  │ flags │  rsv  │   seq (u32)   │   len (u32)   │ payload
 └───────┴───────┴───────┴───────┴───────────────┴───────────────┴──────────
```

| Field | Meaning |
|---|---|
| `ver` | frame format version, `1` |
| `type` | message type, see below |
| `flags` | reserved for future use, send `0` |
| `rsv` | must be `0` |
| `seq` | per-sender counter, for debugging |
| `len` | payload length, at most 65 536 |

Decoding is strict: unknown types, a wrong version, a non-zero reserved byte,
truncated payloads and trailing bytes are all rejected.

## Messages

### Client → server

| Type | Code | Payload |
|---|---|---|
| `NAME` | `0x01` | `str name` |
| `MSG` | `0x02` | `str text` |
| `USERS_REQ` | `0x03` | empty |
| `QUIT` | `0x04` | empty |
| `PING` | `0x05` | empty |
| `PONG` | `0x06` | empty |

### Server → client

| Type | Code | Payload |
|---|---|---|
| `WELCOME` | `0x81` | `str text` |
| `CHAT` | `0x82` | `u64 timestamp_ms` · `str name` · `str text` |
| `JOIN` | `0x83` | `str name` |
| `LEAVE` | `0x84` | `str name` |
| `USERS` | `0x85` | `u16 count` · `count × str name` |
| `ERR` | `0x86` | `u16 code` · `str text` |

### Error codes

| Code | Name | When |
|---|---|---|
| 1 | `PROTOCOL` | malformed frame or unexpected type (connection is closed) |
| 2 | `NAME_INVALID` | name breaks the rules below |
| 3 | `NAME_TAKEN` | another member uses the name (case-insensitive) |
| 4 | `NOT_REGISTERED` | a frame other than `NAME`, `PING` or `QUIT` before `NAME` |
| 5 | `ALREADY_REGISTERED` | second `NAME` on the same connection |
| 6 | `ROOM_FULL` | member limit reached (connection is closed) |
| 7 | `TEXT_INVALID` | empty, too long, or contains control characters |

## Session flow

```
client                         server
  ── handshake ──────────────────
  NAME "ali"          ─────▶
                      ◀─────   WELCOME "Welcome, ali! 2 user(s) online."
                      ◀─────   USERS ["ali", "veli"]
                               JOIN "ali"   ─────▶ other members
  MSG "selam"         ─────▶
                      ◀─────   CHAT (ts, "ali", "selam")  ─────▶ everyone
  QUIT (TAG_FINAL)    ─────▶
                               LEAVE "ali"  ─────▶ other members
```

The sender receives its own `CHAT` back: the server is the single source of
truth for what was said and in which order.

## Content rules

| Item | Rule |
|---|---|
| name | 2–24 bytes of valid UTF-8; ASCII letters, digits, `_`, `-`, `.` or any non-ASCII character (`Şükrü` is fine); no spaces |
| text | 1–2000 bytes of valid UTF-8; no control characters except tab |
| members | at most 256 per room (`-m` on the server) |

Control characters are rejected so nobody can inject ANSI escape sequences
into other people's terminals. Clients still sanitize what they display.

## Trust

The server's identity key never changes unless its key file is replaced.
Clients remember it per `host:port` the first time they connect:

| Client | Where | First contact |
|---|---|---|
| terminal | `~/.config/tcp-chat/known_servers` | saved automatically, fingerprint printed |
| desktop app | same file | asks the user |
| browser | `localStorage` | saved automatically, fingerprint shown |

The fingerprint is BLAKE2b-256 of the public key, printed as 16 groups of 4
hex digits. It is logged by the server at start-up so it can be compared out
of band. If a later connection presents a different key, every client
refuses to continue and shows both fingerprints.
