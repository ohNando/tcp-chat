import { test } from 'node:test'
import assert from 'node:assert/strict'
import sodiumModule from 'libsodium-wrappers'

import { seal, open, sealWithNonce, safetyNumber, textValid, loadOrCreateUserKey, DmError } from './dm.js'

await sodiumModule.ready
const sodium = sodiumModule
const fromSeed = (fill) => sodium.crypto_box_seed_keypair(new Uint8Array(32).fill(fill))
const ali = fromSeed(1)
const veli = fromSeed(2)

test('matches the C implementation (shared vector in Backend/tests/test_dm.c)', () => {
  const box = sealWithNonce(sodium, ali.privateKey, veli.publicKey, 1700000000000, 'merhaba',
                            new Uint8Array(24).fill(3))
  assert.equal(sodium.to_hex(box), 'bff8aa72b8e0d622b610daa242a21a2d1478922eb6903be26ca241961968ec6116')
  assert.equal(safetyNumber(sodium, ali.publicKey, veli.publicKey),
               '32479 21887 28548 40510 95895 94034 71636 85843')
  assert.equal(safetyNumber(sodium, veli.publicKey, ali.publicKey),
               safetyNumber(sodium, ali.publicKey, veli.publicKey))
})

test('recipient and sender can open, a third party cannot', () => {
  const { nonce, box } = seal(sodium, ali.privateKey, veli.publicKey, 5, 'gizli 🔐')
  assert.deepEqual(open(sodium, veli.privateKey, ali.publicKey, nonce, box), { sentAt: 5, text: 'gizli 🔐' })
  assert.equal(open(sodium, ali.privateKey, veli.publicKey, nonce, box).text, 'gizli 🔐')
  const server = fromSeed(3)
  assert.throws(() => open(sodium, server.privateKey, ali.publicKey, nonce, box), DmError)
  box[0] ^= 1
  assert.throws(() => open(sodium, veli.privateKey, ali.publicKey, nonce, box), DmError)
})

test('text rules match the server', () => {
  assert.ok(textValid('tab\tok'))
  assert.ok(!textValid(''))
  assert.ok(!textValid('\x1b[2J'))
  assert.ok(!textValid('x'.repeat(2001)))
  assert.throws(() => seal(sodium, ali.privateKey, veli.publicKey, 1, 'a\nb'), DmError)
})

test('user key is created once and then reused', () => {
  const data = new Map()
  const storage = { getItem: (k) => data.get(k) ?? null, setItem: (k, v) => data.set(k, v) }
  const a = loadOrCreateUserKey(sodium, storage)
  const b = loadOrCreateUserKey(sodium, storage)
  assert.deepEqual([...a.publicKey], [...b.publicKey])
})
