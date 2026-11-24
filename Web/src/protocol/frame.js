// Binary frame and message codec. Mirrors Backend/src/frame.c and
// message.c byte for byte: any change here must be made there too.
//
// Frame header (12 bytes, big-endian):
//   ver u8 | type u8 | flags u8 | reserved u8 | seq u32 | payload len u32
// Strings are u16 length + UTF-8 bytes.

export const FRAME_VERSION = 1
export const FRAME_HEADER_SIZE = 12
export const FRAME_MAX_PAYLOAD = 64 * 1024

export const NAME_MAX_LEN = 24
export const TEXT_MAX_LEN = 2000
export const USERS_MAX = 256

export const DM_KEY_BYTES = 32
export const DM_NONCE_BYTES = 24
// Poly1305 tag (16) + plaintext (u64 sent_at + str text)
export const DM_BOX_MAX = 16 + 8 + 2 + TEXT_MAX_LEN

export const FT = Object.freeze({
  // client -> server
  NAME: 0x01,
  MSG: 0x02,
  USERS_REQ: 0x03,
  QUIT: 0x04,
  PING: 0x05,
  PONG: 0x06,
  TYPING: 0x07,
  KEY: 0x08,
  DM: 0x09,
  // server -> client
  WELCOME: 0x81,
  CHAT: 0x82,
  JOIN: 0x83,
  LEAVE: 0x84,
  USERS: 0x85,
  ERR: 0x86,
  USER_TYPING: 0x87,
  PEER_KEY: 0x88,
  DM_RECV: 0x89,
})

// Frame flags (header byte 2). Unknown flags are ignored.
export const FLAG = Object.freeze({
  HISTORY: 0x01, // CHAT replayed from history, not live
})

export const ERR = Object.freeze({
  PROTOCOL: 1,
  NAME_INVALID: 2,
  NAME_TAKEN: 3,
  NOT_REGISTERED: 4,
  ALREADY_REGISTERED: 5,
  ROOM_FULL: 6,
  TEXT_INVALID: 7,
  NO_SUCH_USER: 8,
  NO_KEY: 9,
})

const KNOWN_TYPES = new Set(Object.values(FT))

export class ProtocolError extends Error {}

const encoder = new TextEncoder()
// fatal: invalid UTF-8 throws instead of becoming U+FFFD.
const decoder = new TextDecoder('utf-8', { fatal: true })

class Writer {
  constructor() {
    this.buf = new Uint8Array(256)
    this.view = new DataView(this.buf.buffer)
    this.pos = 0
  }

  reserve(n) {
    if (this.pos + n <= this.buf.length) return
    let cap = this.buf.length * 2
    while (cap < this.pos + n) cap *= 2
    const next = new Uint8Array(cap)
    next.set(this.buf)
    this.buf = next
    this.view = new DataView(next.buffer)
  }

  u8(v) { this.reserve(1); this.view.setUint8(this.pos, v); this.pos += 1 }
  u16(v) { this.reserve(2); this.view.setUint16(this.pos, v); this.pos += 2 }
  u32(v) { this.reserve(4); this.view.setUint32(this.pos, v); this.pos += 4 }
  u64(v) { this.reserve(8); this.view.setBigUint64(this.pos, BigInt(v)); this.pos += 8 }

  str(s, maxBytes) {
    const bytes = encoder.encode(s)
    if (bytes.length > maxBytes) throw new ProtocolError(`string longer than ${maxBytes} bytes`)
    this.u16(bytes.length)
    this.reserve(bytes.length)
    this.buf.set(bytes, this.pos)
    this.pos += bytes.length
  }

  raw(bytes) {
    this.reserve(bytes.length)
    this.buf.set(bytes, this.pos)
    this.pos += bytes.length
  }

  bytes() { return this.buf.slice(0, this.pos) }
}

class Reader {
  constructor(bytes) {
    this.bytes = bytes
    this.view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength)
    this.pos = 0
  }

  need(n) {
    if (this.pos + n > this.bytes.length) throw new ProtocolError('truncated payload')
  }

  u8() { this.need(1); return this.view.getUint8(this.pos++) }
  u16() { this.need(2); const v = this.view.getUint16(this.pos); this.pos += 2; return v }
  u32() { this.need(4); const v = this.view.getUint32(this.pos); this.pos += 4; return v }
  u64() { this.need(8); const v = this.view.getBigUint64(this.pos); this.pos += 8; return v }

  str(maxBytes) {
    const n = this.u16()
    if (n > maxBytes) throw new ProtocolError(`string longer than ${maxBytes} bytes`)
    this.need(n)
    const s = this.bytes.subarray(this.pos, this.pos + n)
    this.pos += n
    try {
      return decoder.decode(s)
    } catch {
      throw new ProtocolError('invalid UTF-8')
    }
  }

  raw(n) {
    this.need(n)
    const out = this.bytes.slice(this.pos, this.pos + n)
    this.pos += n
    return out
  }

  get remaining() { return this.bytes.length - this.pos }
}

function fixed(bytes, n, what) {
  if (!(bytes instanceof Uint8Array) || bytes.length !== n)
    throw new ProtocolError(`${what} must be ${n} bytes`)
  return bytes
}

// nonce[24] | u16 len | box[len]
function writeBox(w, m) {
  fixed(m.nonce, DM_NONCE_BYTES, 'nonce')
  if (!(m.box instanceof Uint8Array) || m.box.length > DM_BOX_MAX)
    throw new ProtocolError('box too large')
  w.raw(m.nonce)
  w.u16(m.box.length)
  w.raw(m.box)
}

function readBox(r, m) {
  m.nonce = r.raw(DM_NONCE_BYTES)
  const len = r.u16()
  if (len > DM_BOX_MAX) throw new ProtocolError('box too large')
  m.box = r.raw(len)
}

function writePayload(w, m) {
  switch (m.type) {
    case FT.NAME:
    case FT.JOIN:
    case FT.LEAVE:
      w.str(m.name, NAME_MAX_LEN)
      break
    case FT.MSG:
    case FT.WELCOME:
      w.str(m.text, TEXT_MAX_LEN)
      break
    case FT.CHAT:
      w.u64(m.timestamp)
      w.str(m.name, NAME_MAX_LEN)
      w.str(m.text, TEXT_MAX_LEN)
      break
    case FT.USERS:
      if (m.users.length > USERS_MAX) throw new ProtocolError('too many users')
      w.u16(m.users.length)
      for (const u of m.users) w.str(u, NAME_MAX_LEN)
      break
    case FT.ERR:
      w.u16(m.code)
      w.str(m.text, TEXT_MAX_LEN)
      break
    case FT.TYPING:
      w.u8(m.active ? 1 : 0)
      break
    case FT.USER_TYPING:
      w.str(m.name, NAME_MAX_LEN)
      w.u8(m.active ? 1 : 0)
      break
    case FT.KEY:
      w.raw(fixed(m.key, DM_KEY_BYTES, 'key'))
      break
    case FT.PEER_KEY:
      w.str(m.name, NAME_MAX_LEN)
      w.raw(fixed(m.key, DM_KEY_BYTES, 'key'))
      break
    case FT.DM:
      w.str(m.to, NAME_MAX_LEN)
      writeBox(w, m)
      break
    case FT.DM_RECV:
      w.u64(m.timestamp)
      w.str(m.name, NAME_MAX_LEN)
      w.str(m.to, NAME_MAX_LEN)
      writeBox(w, m)
      break
    case FT.USERS_REQ:
    case FT.QUIT:
    case FT.PING:
    case FT.PONG:
      break
    default:
      throw new ProtocolError(`unknown frame type ${m.type}`)
  }
}

function readPayload(r, type) {
  const m = { type }
  switch (type) {
    case FT.NAME:
    case FT.JOIN:
    case FT.LEAVE:
      m.name = r.str(NAME_MAX_LEN)
      break
    case FT.MSG:
    case FT.WELCOME:
      m.text = r.str(TEXT_MAX_LEN)
      break
    case FT.CHAT:
      m.timestamp = Number(r.u64())
      m.name = r.str(NAME_MAX_LEN)
      m.text = r.str(TEXT_MAX_LEN)
      break
    case FT.USERS: {
      const count = r.u16()
      if (count > USERS_MAX) throw new ProtocolError('too many users')
      m.users = []
      for (let i = 0; i < count; i++) m.users.push(r.str(NAME_MAX_LEN))
      break
    }
    case FT.ERR:
      m.code = r.u16()
      m.text = r.str(TEXT_MAX_LEN)
      break
    case FT.TYPING:
      m.active = r.u8() !== 0
      break
    case FT.USER_TYPING:
      m.name = r.str(NAME_MAX_LEN)
      m.active = r.u8() !== 0
      break
    case FT.KEY:
      m.key = r.raw(DM_KEY_BYTES)
      break
    case FT.PEER_KEY:
      m.name = r.str(NAME_MAX_LEN)
      m.key = r.raw(DM_KEY_BYTES)
      break
    case FT.DM:
      m.to = r.str(NAME_MAX_LEN)
      readBox(r, m)
      break
    case FT.DM_RECV:
      m.timestamp = Number(r.u64())
      m.name = r.str(NAME_MAX_LEN)
      m.to = r.str(NAME_MAX_LEN)
      readBox(r, m)
      break
    default:
      break
  }
  return m
}

/** Encodes a message object ({ type, ...fields }) into a complete frame. */
export function encodeMessage(m, seq = 0) {
  const body = new Writer()
  writePayload(body, m)
  const payload = body.bytes()
  if (payload.length > FRAME_MAX_PAYLOAD) throw new ProtocolError('payload too large')

  const out = new Uint8Array(FRAME_HEADER_SIZE + payload.length)
  const view = new DataView(out.buffer)
  view.setUint8(0, FRAME_VERSION)
  view.setUint8(1, m.type)
  view.setUint8(2, m.flags ?? 0)
  view.setUint8(3, 0) // reserved
  view.setUint32(4, seq >>> 0)
  view.setUint32(8, payload.length)
  out.set(payload, FRAME_HEADER_SIZE)
  return out
}

/** Decodes a complete frame. Strict, like the C decoder. */
export function decodeMessage(frame) {
  if (frame.length < FRAME_HEADER_SIZE) throw new ProtocolError('frame too short')
  const view = new DataView(frame.buffer, frame.byteOffset, frame.byteLength)
  const version = view.getUint8(0)
  const type = view.getUint8(1)
  const flags = view.getUint8(2)
  const reserved = view.getUint8(3)
  const seq = view.getUint32(4)
  const len = view.getUint32(8)

  if (version !== FRAME_VERSION) throw new ProtocolError('unsupported frame version')
  if (reserved !== 0) throw new ProtocolError('reserved byte set')
  if (!KNOWN_TYPES.has(type)) throw new ProtocolError(`unknown frame type ${type}`)
  if (len > FRAME_MAX_PAYLOAD) throw new ProtocolError('payload too large')
  if (frame.length - FRAME_HEADER_SIZE !== len) throw new ProtocolError('length mismatch')

  const r = new Reader(frame.subarray(FRAME_HEADER_SIZE))
  const m = readPayload(r, type)
  if (r.remaining !== 0) throw new ProtocolError('trailing bytes')
  m.seq = seq
  m.flags = flags
  return m
}
