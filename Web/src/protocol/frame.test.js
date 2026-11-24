import { test } from 'node:test'
import assert from 'node:assert/strict'

import { FT, ERR, FLAG, FRAME_HEADER_SIZE, encodeMessage, decodeMessage, ProtocolError } from './frame.js'

const roundTrip = (m) => decodeMessage(encodeMessage(m, 7))

test('header has the same wire layout as the C server', () => {
  const f = encodeMessage({ type: FT.JOIN, name: 'ali' }, 0x01020304)
  assert.deepEqual([...f.subarray(0, FRAME_HEADER_SIZE)],
    [1, FT.JOIN, 0, 0, 1, 2, 3, 4, 0, 0, 0, 5])
  assert.deepEqual([...f.subarray(FRAME_HEADER_SIZE)], [0, 3, 0x61, 0x6c, 0x69])
})

test('every message type round-trips', () => {
  assert.equal(roundTrip({ type: FT.NAME, name: 'Şükrü' }).name, 'Şükrü')
  assert.equal(roundTrip({ type: FT.MSG, text: 'merhaba dünya 😀' }).text, 'merhaba dünya 😀')
  assert.equal(roundTrip({ type: FT.WELCOME, text: 'hi' }).text, 'hi')

  const chat = roundTrip({ type: FT.CHAT, timestamp: 1763900000123, name: 'ali', text: 'selam' })
  assert.deepEqual([chat.timestamp, chat.name, chat.text, chat.seq], [1763900000123, 'ali', 'selam', 7])

  assert.deepEqual(roundTrip({ type: FT.USERS, users: ['a', 'bb'] }).users, ['a', 'bb'])
  const err = roundTrip({ type: FT.ERR, code: ERR.NAME_TAKEN, text: 'taken' })
  assert.deepEqual([err.code, err.text], [ERR.NAME_TAKEN, 'taken'])

  for (const type of [FT.USERS_REQ, FT.QUIT, FT.PING, FT.PONG])
    assert.equal(encodeMessage({ type }).length, FRAME_HEADER_SIZE)
})

test('decoder rejects malformed frames', () => {
  const good = encodeMessage({ type: FT.NAME, name: 'ali' })

  assert.throws(() => decodeMessage(good.subarray(0, good.length - 1)), ProtocolError)

  const withTrailing = new Uint8Array(good.length + 1)
  withTrailing.set(good)
  withTrailing[11] += 1
  assert.throws(() => decodeMessage(withTrailing), ProtocolError)

  const badVersion = good.slice(); badVersion[0] = 9
  assert.throws(() => decodeMessage(badVersion), ProtocolError)

  const badType = good.slice(); badType[1] = 0x7f
  assert.throws(() => decodeMessage(badType), ProtocolError)

  const badUtf8 = encodeMessage({ type: FT.NAME, name: 'ab' })
  badUtf8[FRAME_HEADER_SIZE + 2] = 0xff
  assert.throws(() => decodeMessage(badUtf8), ProtocolError)
})

test('length limits match the server', () => {
  assert.throws(() => encodeMessage({ type: FT.NAME, name: 'x'.repeat(25) }), ProtocolError)
  assert.doesNotThrow(() => encodeMessage({ type: FT.NAME, name: 'x'.repeat(24) }))
  assert.throws(() => encodeMessage({ type: FT.MSG, text: 'x'.repeat(2001) }), ProtocolError)
})

test('flags round-trip and default to zero', () => {
  const h = roundTrip({ type: FT.CHAT, flags: FLAG.HISTORY, timestamp: 1, name: 'ali', text: 'eski' })
  assert.equal(h.flags, FLAG.HISTORY)
  assert.equal(roundTrip({ type: FT.CHAT, timestamp: 1, name: 'ali', text: 'yeni' }).flags, 0)
})

test('typing messages round-trip', () => {
  assert.equal(roundTrip({ type: FT.TYPING, active: true }).active, true)
  const t = roundTrip({ type: FT.USER_TYPING, name: 'ayşe', active: false })
  assert.deepEqual([t.name, t.active], ['ayşe', false])
})

test('key and dm messages round-trip', () => {
  const key = new Uint8Array(32).map((_, i) => i)
  assert.deepEqual([...roundTrip({ type: FT.KEY, key }).key], [...key])
  const pk = roundTrip({ type: FT.PEER_KEY, name: 'veli', key })
  assert.equal(pk.name, 'veli')

  const nonce = new Uint8Array(24).fill(7)
  const box = new Uint8Array(40).fill(9)
  const dm = roundTrip({ type: FT.DM_RECV, timestamp: 42, name: 'ali', to: 'veli', nonce, box })
  assert.deepEqual([dm.timestamp, dm.name, dm.to], [42, 'ali', 'veli'])
  assert.deepEqual([...dm.nonce], [...nonce])
  assert.deepEqual([...dm.box], [...box])

  assert.throws(() => encodeMessage({ type: FT.KEY, key: new Uint8Array(31) }), ProtocolError)
})
