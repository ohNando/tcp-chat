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
| `flags` | bit field, see below; unknown bits are ignored |
| `rsv` | must be `0` |
| `seq` | per-sender counter, for debugging |
| `len` | payload length, at most 65 536 |

| Flag | Value | Meaning |
|---|---|---|
| `HISTORY` | `0x01` | a `CHAT` replayed from the server's recent history, not a live message |

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
| `TYPING` | `0x07` | `u8 active` (1 = typing, 0 = stopped) |
| `KEY` | `0x08` | `key[32]`: the user's X25519 public key for private messages |
| `DM` | `0x09` | `str to` · `nonce[24]` · `u16 len` · `box[len]` |

`PING` and `PONG` work in both directions: the receiver of a `PING`
answers with a `PONG`.

### Heartbeat

The server sends a `PING` to any connection that has been silent for 40 %
of its idle timeout (75 s by default, `-t` on the server), and closes a
connection that sends nothing at all for the whole timeout. This removes
dead peers (pulled cable, suspended laptop) that TCP alone would keep open
forever. Clients must answer with `PONG`; any other traffic also counts.

### Server → client

| Type | Code | Payload |
|---|---|---|
| `WELCOME` | `0x81` | `str text` |
| `CHAT` | `0x82` | `u64 timestamp_ms` · `str name` · `str text` |
| `JOIN` | `0x83` | `str name` |
| `LEAVE` | `0x84` | `str name` |
| `USERS` | `0x85` | `u16 count` · `count × str name` |
| `ERR` | `0x86` | `u16 code` · `str text` |
| `USER_TYPING` | `0x87` | `str name` · `u8 active` |
| `PEER_KEY` | `0x88` | `str name` · `key[32]` |
| `DM_RECV` | `0x89` | `u64 timestamp_ms` · `str from` · `str to` · `nonce[24]` · `u16 len` · `box[len]` |

The server forwards a member's `TYPING` to everybody else as
`USER_TYPING`. Clients send `active = 1` at most every 2 seconds while the
user types and `active = 0` when the input is cleared or sent; a receiver
hides the indicator if it is not refreshed within 5 seconds.

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
| 8 | `NO_SUCH_USER` | a `DM` names someone who is not in the room |
| 9 | `NO_KEY` | a `DM` when the sender or the recipient has not sent `KEY` yet |

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

## Private messages (end-to-end encrypted)

Private messages are encrypted by the sender for the recipient; the server
only relays the ciphertext and cannot read it.

**Keys.** Every user has a long-term X25519 key pair (`crypto_box`), kept on
the device (`~/.config/tcp-chat/user.key` for the terminal and desktop
clients, `localStorage` in the browser). After `WELCOME` a client sends its
public key in `KEY`. The server forwards it to everybody else as `PEER_KEY`
and sends each newcomer a `PEER_KEY` for every member that already has one.

**Box.** The plaintext is `u64 sent_at_ms | str text`. It is encrypted with
`crypto_box_easy(plaintext, nonce, recipient_pk, sender_sk)`
(X25519 + XSalsa20-Poly1305) and a random 24-byte nonce. The server sends
the resulting `DM_RECV` to the recipient **and back to the sender**: both
derive the same shared key, so the sender can open its own copy, and every
client shows what the server actually delivered.

The recipient checks the text with the same rules as chat text, because the
server cannot.

**Safety number.** The server hands out the keys, so a malicious server
could substitute its own. Clients therefore show a safety number for each
conversation: BLAKE2b-256 over `"TCHT-safety-v1"` followed by the two public
keys in ascending byte order, printed as eight groups of five digits
(each group is a big-endian u32 of the hash modulo 100000). Both people see
the same number and can compare it out of band. Clients also remember each
`host:port/name`'s key and warn if it changes.

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
