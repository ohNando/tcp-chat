// End-to-end encrypted private messages. Mirrors Backend/src/dm.c.
//
//   box = crypto_box_easy(u64 sent_at | str text, nonce, peer_pk, my_sk)
//
// The server only relays nonce + box. Both sides derive the same shared
// key, so the sender can open its own echo too.

import { TEXT_MAX_LEN, DM_NONCE_BYTES } from './frame.js'

const encoder = new TextEncoder()
const decoder = new TextDecoder('utf-8', { fatal: true })
const SAFETY_CONTEXT = encoder.encode('TCHT-safety-v1')
const STORAGE_KEY = 'tcpchat.user_key'

export class DmError extends Error {}

/** Same rules as validate.c's text_valid(). */
export function textValid(text) {
  const bytes = encoder.encode(text)
  if (bytes.length === 0 || bytes.length > TEXT_MAX_LEN) return false
  // eslint-disable-next-line no-control-regex
  return !/[\u0000-\u0008\u000a-\u001f\u007f]/.test(text)
}

/** The user's long-term key pair, kept as a seed in `storage`. */
export function loadOrCreateUserKey(sodium, storage) {
  let seed = null
  try {
    const saved = storage.getItem(STORAGE_KEY)
    if (saved) seed = sodium.from_base64(saved)
  } catch {
    seed = null
  }
  if (!seed || seed.length !== sodium.crypto_box_SEEDBYTES) {
    seed = sodium.randombytes_buf(sodium.crypto_box_SEEDBYTES)
    storage.setItem(STORAGE_KEY, sodium.to_base64(seed))
  }
  return sodium.crypto_box_seed_keypair(seed)
}

function plaintext(sentAt, text) {
  const t = encoder.encode(text)
  const out = new Uint8Array(8 + 2 + t.length)
  const view = new DataView(out.buffer)
  view.setBigUint64(0, BigInt(sentAt))
  view.setUint16(8, t.length)
  out.set(t, 10)
  return out
}

export function seal(sodium, mySk, peerPk, sentAt, text) {
  if (!textValid(text)) throw new DmError('message is empty, too long or has control characters')
  const nonce = sodium.randombytes_buf(DM_NONCE_BYTES)
  const box = sodium.crypto_box_easy(plaintext(sentAt, text), nonce, peerPk, mySk)
  return { nonce, box }
}

/** For tests: sealing with a given nonce. */
export function sealWithNonce(sodium, mySk, peerPk, sentAt, text, nonce) {
  return sodium.crypto_box_easy(plaintext(sentAt, text), nonce, peerPk, mySk)
}

export function open(sodium, mySk, peerPk, nonce, box) {
  let plain
  try {
    plain = sodium.crypto_box_open_easy(box, nonce, peerPk, mySk)
  } catch {
    throw new DmError('cannot decrypt (forged or not meant for us)')
  }
  if (plain.length < 10) throw new DmError('malformed message')
  const view = new DataView(plain.buffer, plain.byteOffset, plain.byteLength)
  const sentAt = Number(view.getBigUint64(0))
  const len = view.getUint16(8)
  if (10 + len !== plain.length) throw new DmError('malformed message')
  let text
  try {
    text = decoder.decode(plain.subarray(10))
  } catch {
    throw new DmError('invalid UTF-8')
  }
  if (!textValid(text)) throw new DmError('message breaks the text rules')
  return { sentAt, text }
}

function compare(a, b) {
  for (let i = 0; i < a.length; i++) if (a[i] !== b[i]) return a[i] - b[i]
  return 0
}

/** Same for both people: BLAKE2b over the sorted keys, 8 groups of 5 digits. */
export function safetyNumber(sodium, a, b) {
  const [lo, hi] = compare(a, b) <= 0 ? [a, b] : [b, a]
  const input = new Uint8Array(SAFETY_CONTEXT.length + 64)
  input.set(SAFETY_CONTEXT)
  input.set(lo, SAFETY_CONTEXT.length)
  input.set(hi, SAFETY_CONTEXT.length + 32)
  const hash = sodium.crypto_generichash(32, input)
  const view = new DataView(hash.buffer, hash.byteOffset, hash.byteLength)
  const groups = []
  for (let i = 0; i < 8; i++) groups.push(String(view.getUint32(4 * i) % 100000).padStart(5, '0'))
  return groups.join(' ')
}
