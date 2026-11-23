import { test } from 'node:test'
import assert from 'node:assert/strict'

import { Pin, checkPin, forgetPin, pinnedFingerprint } from './pins.js'

function memoryStorage() {
  const data = new Map()
  return {
    getItem: (k) => (data.has(k) ? data.get(k) : null),
    setItem: (k, v) => data.set(k, String(v)),
  }
}

test('first contact pins, same key matches, other key mismatches', () => {
  const s = memoryStorage()
  assert.equal(checkPin(s, 'localhost:8081', 'AAAA'), Pin.NEW)
  assert.equal(checkPin(s, 'localhost:8081', 'AAAA'), Pin.MATCH)
  assert.equal(checkPin(s, 'localhost:8081', 'BBBB'), Pin.MISMATCH)
  assert.equal(pinnedFingerprint(s, 'localhost:8081'), 'AAAA')
})

test('hosts are pinned independently', () => {
  const s = memoryStorage()
  checkPin(s, 'a:1', 'AAAA')
  assert.equal(checkPin(s, 'b:1', 'BBBB'), Pin.NEW)
})

test('forgetting a host allows a new key', () => {
  const s = memoryStorage()
  checkPin(s, 'h:1', 'OLD')
  forgetPin(s, 'h:1')
  assert.equal(checkPin(s, 'h:1', 'NEW'), Pin.NEW)
})

test('corrupt storage is treated as empty', () => {
  const s = memoryStorage()
  s.setItem('tcpchat.known_servers', '{not json')
  assert.equal(checkPin(s, 'h:1', 'X'), Pin.NEW)
})
