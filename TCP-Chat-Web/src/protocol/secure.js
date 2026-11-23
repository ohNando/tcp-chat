// Client side of the secure channel. Mirrors TCP-Chat-Backend/src/secure.c
// and handshake.c (see the comments there for the full design).
//
//   C -> S  CLIENT_HELLO  "TCHT" | ver | client_eph_pk[32]
//   S -> C  SERVER_HELLO  "TCHT" | ver | server_eph_pk[32] | server_id_pk[32] | sig[64]
//   S -> C  server secretstream header (24 bytes)
//   C -> S  client secretstream header (24 bytes)
//   then every record is crypto_secretstream_xchacha20poly1305 output.
//
// `sodium` is a ready libsodium-wrappers instance, passed in so this module
// works the same in the browser and in Node tests.

const MAGIC = [0x54, 0x43, 0x48, 0x54] // "TCHT"
const VERSION = 1
const CONTEXT = new TextEncoder().encode('TCHT-handshake-v1')
const KX_PK = 32
const ID_PK = 32
const SIG = 64
const CLIENT_HELLO_LEN = 4 + 1 + KX_PK
const SERVER_HELLO_LEN = 4 + 1 + KX_PK + ID_PK + SIG
const HEADER_BYTES = 24

export class SecureError extends Error {}

/** BLAKE2b-256 of the server key as "XXXX:XXXX:..." (same as the C side). */
export function fingerprint(sodium, pk) {
  const hex = sodium.to_hex(sodium.crypto_generichash(32, pk)).toUpperCase()
  return hex.match(/.{4}/g).join(':')
}

function concat(...parts) {
  const out = new Uint8Array(parts.reduce((n, p) => n + p.length, 0))
  let pos = 0
  for (const p of parts) { out.set(p, pos); pos += p.length }
  return out
}

export const State = Object.freeze({
  WAIT_HELLO: 'wait_hello',
  WAIT_HEADER: 'wait_header',
  ESTABLISHED: 'established',
  PEER_FINISHED: 'peer_finished',
  FAILED: 'failed',
})

export class SecureChannel {
  /**
   * @param sodium  ready libsodium-wrappers instance
   * @param emit    (Uint8Array) => void, sends one record
   * @param verify  (serverIdPk: Uint8Array) => boolean, pin check
   */
  constructor(sodium, { emit, verify }) {
    this.sodium = sodium
    this.emit = emit
    this.verify = verify
    this.state = State.WAIT_HELLO
    this.serverKey = null
    this.txFinished = false
  }

  get established() {
    return this.state === State.ESTABLISHED
  }

  start() {
    const kp = this.sodium.crypto_kx_keypair()
    this.ephPk = kp.publicKey
    this.ephSk = kp.privateKey
    this.emit(concat(MAGIC, [VERSION], this.ephPk))
  }

  fail(reason) {
    this.state = State.FAILED
    if (this.ephSk) this.sodium.memzero(this.ephSk)
    if (this.rxKey) this.sodium.memzero(this.rxKey)
    throw new SecureError(reason)
  }

  /** Feeds one received record. Returns plaintext, or null during the handshake. */
  receive(record) {
    switch (this.state) {
      case State.WAIT_HELLO:
        this.onServerHello(record)
        return null
      case State.WAIT_HEADER:
        this.onHeader(record)
        return null
      case State.ESTABLISHED:
        return this.onData(record)
      case State.PEER_FINISHED:
        return this.fail('record after end of stream')
      default:
        throw new SecureError('channel has failed')
    }
  }

  onServerHello(rec) {
    const s = this.sodium
    if (rec.length !== SERVER_HELLO_LEN) this.fail('bad server hello')
    for (let i = 0; i < 4; i++) if (rec[i] !== MAGIC[i]) this.fail('bad server hello')
    if (rec[4] !== VERSION) this.fail('unsupported protocol version')

    let p = 5
    const serverEph = rec.slice(p, p += KX_PK)
    const serverId = rec.slice(p, p += ID_PK)
    const sig = rec.slice(p, p += SIG)

    const transcript = concat(CONTEXT, this.ephPk, serverEph, serverId)
    if (!s.crypto_sign_verify_detached(sig, transcript, serverId))
      this.fail('invalid server signature')

    this.serverKey = serverId
    if (!this.verify(serverId)) this.fail('server identity not trusted')

    let keys
    try {
      keys = s.crypto_kx_client_session_keys(this.ephPk, this.ephSk, serverEph)
    } catch {
      this.fail('key exchange failed')
    }
    s.memzero(this.ephSk)
    this.rxKey = keys.sharedRx

    const { state, header } = s.crypto_secretstream_xchacha20poly1305_init_push(keys.sharedTx)
    s.memzero(keys.sharedTx)
    this.tx = state
    this.emit(header)
    this.state = State.WAIT_HEADER
  }

  onHeader(rec) {
    if (rec.length !== HEADER_BYTES) this.fail('bad stream header')
    try {
      this.rx = this.sodium.crypto_secretstream_xchacha20poly1305_init_pull(rec, this.rxKey)
    } catch {
      this.fail('bad stream header')
    }
    this.sodium.memzero(this.rxKey)
    this.state = State.ESTABLISHED
  }

  onData(rec) {
    const s = this.sodium
    let res
    try {
      res = s.crypto_secretstream_xchacha20poly1305_pull(this.rx, rec)
    } catch {
      res = null
    }
    if (!res) this.fail('decryption failed (tampered, replayed or reordered record)')
    if (res.tag === s.crypto_secretstream_xchacha20poly1305_TAG_FINAL)
      this.state = State.PEER_FINISHED
    return res.message
  }

  /** Encrypts and emits one record; `final` ends our side of the stream. */
  send(plain, final = false) {
    const s = this.sodium
    if (this.txFinished) throw new SecureError('stream already finished')
    if (this.state !== State.ESTABLISHED && this.state !== State.PEER_FINISHED)
      throw new SecureError('handshake not complete')
    const tag = final
      ? s.crypto_secretstream_xchacha20poly1305_TAG_FINAL
      : s.crypto_secretstream_xchacha20poly1305_TAG_MESSAGE
    if (final) this.txFinished = true
    this.emit(s.crypto_secretstream_xchacha20poly1305_push(this.tx, plain, null, tag))
  }
}
