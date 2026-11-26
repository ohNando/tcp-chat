// End-to-end test against the real C server.
//
// Starts Backend/bin/server in a temporary directory, then joins
// the same room with one WebSocket client and one raw TCP client, both using
// the JavaScript protocol + secure channel. Run: npm run e2e
// (build the server first with `make` in Backend).

import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { mkdtempSync, rmSync } from 'node:fs'
import net from 'node:net'
import { tmpdir } from 'node:os'
import { join, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'
import sodiumModule from 'libsodium-wrappers'

import { FT, ERR, FLAG, encodeMessage, decodeMessage } from '../src/protocol/frame.js'
import { SecureChannel, SecureError, fingerprint } from '../src/protocol/secure.js'
import { seal, open } from '../src/protocol/dm.js'

const here = dirname(fileURLToPath(import.meta.url))
const SERVER = join(here, '../../Backend/bin/server')
const TCP_PORT = 18080 + Math.floor(Math.random() * 1000)
const WS_PORT = TCP_PORT + 1000

await sodiumModule.ready
const sodium = sodiumModule

/* ---------------- server process ---------------- */

const workDir = mkdtempSync(join(tmpdir(), 'tcpchat-e2e-'))
// A short idle timeout (pings after 1.2 s, drops after 3 s) lets the
// heartbeat be tested quickly.
const IDLE_TIMEOUT_S = 3
const RATE = 20, BURST = RATE * 4
let server
let serverLog = ''
// Started again later in the same directory: same key, same history file.
function startServer() {
  // 20 frames/s with bursts of 80: enough for the bulk messages below,
  // low enough to test flood protection quickly.
  server = spawn(SERVER, ['-p', String(TCP_PORT), '-w', String(WS_PORT), '-t', String(IDLE_TIMEOUT_S),
                          '-r', String(RATE)],
                 { cwd: workDir })
  const startedAt = serverLog.length
  return new Promise((resolve, reject) => {
    server.on('error', reject)
    server.stdout.on('data', (d) => {
      serverLog += d
      const log = serverLog.slice(startedAt)
      const fp = log.match(/server fingerprint: (\S+)/)
      if (fp && log.includes('listening on port')) resolve(fp[1])
    })
    setTimeout(() => reject(new Error('server did not start:\n' + serverLog)), 5000)
  })
}
async function stopServer() {
  server.kill('SIGINT')
  await new Promise((r) => server.on('exit', r))
}
const serverFingerprint = await startServer()

/* ---------------- client helper ---------------- */

// A chat client over either transport. Received messages are queued so the
// test can await them in order.
class Client {
  constructor(name, { pin = serverFingerprint, answerPings = true } = {}) {
    this.name = name
    this.answerPings = answerPings
    this.inbox = []
    this.waiters = []
    this.seq = 0
    this.closed = false
    this.channel = new SecureChannel(sodium, {
      emit: (rec) => this.sendRecord(rec),
      verify: (pk) => fingerprint(sodium, pk) === pin,
    })
  }

  push(item) {
    const w = this.waiters.shift()
    if (w) w(item)
    else this.inbox.push(item)
  }

  next(timeoutMs = 2000) {
    if (this.inbox.length) return Promise.resolve(this.inbox.shift())
    return new Promise((resolve, reject) => {
      const t = setTimeout(() => reject(new Error(`${this.name}: timed out waiting`)), timeoutMs)
      this.waiters.push((item) => { clearTimeout(t); resolve(item) })
    })
  }

  async expect(type) {
    const m = await this.next()
    assert.equal(m.type, type, `${this.name}: expected ${type}, got ${JSON.stringify(m)}`)
    return m
  }

  // Skips other messages until one of `type` arrives.
  async waitFor(type, timeoutMs = 2000) {
    for (;;) {
      const m = await this.next(timeoutMs)
      if (m.type === type) return m
    }
  }

  onRecord(rec) {
    try {
      const plain = this.channel.receive(rec)
      if (plain === null) {
        if (this.channel.established) this.push({ type: 'established' })
        return
      }
      const m = decodeMessage(plain)
      if (m.type === FT.PING) {
        // Real clients answer the server's heartbeat; a "dead" one does not.
        if (this.answerPings) this.send({ type: FT.PONG })
        return
      }
      this.push(m)
    } catch (e) {
      this.push({ type: 'error', error: e })
    }
  }

  send(m, final = false) {
    this.channel.send(encodeMessage(m, this.seq++), final)
  }
}

class WsClient extends Client {
  connect() {
    this.ws = new WebSocket(`ws://127.0.0.1:${WS_PORT}/`)
    this.ws.binaryType = 'arraybuffer'
    this.ws.onopen = () => this.channel.start()
    this.ws.onmessage = (e) => this.onRecord(new Uint8Array(e.data))
    this.ws.onclose = () => { this.closed = true; this.push({ type: 'closed' }) }
    return this
  }
  sendRecord(rec) { this.ws.send(rec) }
  close() { this.ws.close() }
}

class TcpClient extends Client {
  connect() {
    this.buf = new Uint8Array(0)
    this.sock = net.connect(TCP_PORT, '127.0.0.1', () => this.channel.start())
    this.sock.on('data', (d) => {
      const merged = new Uint8Array(this.buf.length + d.length)
      merged.set(this.buf)
      merged.set(d, this.buf.length)
      this.buf = merged
      // u32 length prefix framing
      while (this.buf.length >= 4) {
        const n = new DataView(this.buf.buffer, this.buf.byteOffset).getUint32(0)
        if (this.buf.length < 4 + n) break
        this.onRecord(this.buf.slice(4, 4 + n))
        this.buf = this.buf.slice(4 + n)
      }
    })
    this.sock.on('close', () => { this.closed = true; this.push({ type: 'closed' }) })
    // A server closing on us while we still send gets a reset: that is a close too.
    this.sock.on('error', () => {})
    return this
  }
  sendRecord(rec) {
    const hdr = new Uint8Array(4)
    new DataView(hdr.buffer).setUint32(0, rec.length)
    this.sock.write(hdr)
    this.sock.write(rec)
  }
  close() { this.sock.end() }
}

/* ---------------- scenario ---------------- */

const steps = []
const step = (name, fn) => steps.push([name, fn])

const alice = new WsClient('alice')
const bob = new TcpClient('bob')
const firstIds = [] // ids of the first two chats, checked after a restart

step('websocket client completes the handshake', async () => {
  alice.connect()
  await alice.expect('established')
})

step('websocket client registers a name', async () => {
  alice.send({ type: FT.NAME, name: 'alice' })
  // Empty history: just its end marker, then the welcome.
  const end = await alice.expect(FT.HISTORY_END)
  assert.deepEqual([end.more, end.flags], [false, FLAG.HISTORY])
  const w = await alice.expect(FT.WELCOME)
  assert.match(w.text, /Welcome, alice/)
  assert.deepEqual((await alice.expect(FT.USERS)).users, ['alice'])
})

step('tcp client joins the same room', async () => {
  bob.connect()
  await bob.expect('established')
  bob.send({ type: FT.NAME, name: 'bob' })
  await bob.expect(FT.HISTORY_END)
  await bob.expect(FT.WELCOME)
  assert.deepEqual((await bob.expect(FT.USERS)).users, ['alice', 'bob'])
  assert.equal((await alice.expect(FT.JOIN)).name, 'bob')
})

step('chat crosses transports and reaches the sender too', async () => {
  alice.send({ type: FT.MSG, text: 'merhaba from the browser 🌍' })
  for (const c of [alice, bob]) {
    const m = await c.expect(FT.CHAT)
    if (c === alice) firstIds.push(m.id)
    assert.equal(m.name, 'alice')
    assert.equal(m.text, 'merhaba from the browser 🌍')
    assert.ok(Math.abs(Date.now() - m.timestamp) < 5000)
  }
  bob.send({ type: FT.MSG, text: 'hi from the terminal' })
  const second = await alice.expect(FT.CHAT)
  assert.equal(second.text, 'hi from the terminal')
  assert.equal((await bob.expect(FT.CHAT)).id, second.id, 'everybody sees the same id')
  assert.ok(second.id > 0)
  firstIds.push(second.id)
})

step('a new member receives recent history before the welcome', async () => {
  const erin = new WsClient('erin').connect()
  await erin.expect('established')
  erin.send({ type: FT.NAME, name: 'erin' })
  const first = await erin.expect(FT.CHAT)
  const second = await erin.expect(FT.CHAT)
  assert.deepEqual([first.name, first.text, first.flags], ['alice', 'merhaba from the browser 🌍', FLAG.HISTORY])
  assert.deepEqual([second.name, second.text, second.flags], ['bob', 'hi from the terminal', FLAG.HISTORY])
  assert.deepEqual([first.id, second.id], firstIds, 'history keeps the original ids')
  assert.equal((await erin.expect(FT.HISTORY_END)).more, false)
  await erin.expect(FT.WELCOME)
  erin.send({ type: FT.QUIT }, true)
  await erin.waitFor('closed')
  assert.equal((await alice.expect(FT.JOIN)).name, 'erin')
  assert.equal((await alice.expect(FT.LEAVE)).name, 'erin')
  await bob.waitFor(FT.LEAVE)
})

step('live messages are not flagged as history', async () => {
  alice.send({ type: FT.MSG, text: 'live' })
  assert.equal((await alice.expect(FT.CHAT)).flags, 0)
  await bob.waitFor(FT.CHAT)
})

step('typing notifications reach everybody but the typist', async () => {
  alice.send({ type: FT.TYPING, active: true })
  const t = await bob.expect(FT.USER_TYPING)
  assert.deepEqual([t.name, t.active], ['alice', true])
  alice.send({ type: FT.TYPING, active: false })
  assert.equal((await bob.expect(FT.USER_TYPING)).active, false)
  // alice must not get her own notifications back
  alice.send({ type: FT.USERS_REQ })
  assert.equal((await alice.next()).type, FT.USERS)
})

step('duplicate names are rejected', async () => {
  const carol = new WsClient('carol').connect()
  await carol.expect('established')
  carol.send({ type: FT.NAME, name: 'ALICE' })
  assert.equal((await carol.expect(FT.ERR)).code, ERR.NAME_TAKEN)
  carol.close()
  await carol.expect('closed')
})

step('users request lists both members', async () => {
  bob.send({ type: FT.USERS_REQ })
  assert.deepEqual((await bob.expect(FT.USERS)).users, ['alice', 'bob'])
})

step('private messages are end-to-end encrypted', async () => {
  const aliceKey = sodium.crypto_box_keypair()
  const bobKey = sodium.crypto_box_keypair()
  alice.send({ type: FT.KEY, key: aliceKey.publicKey })
  bob.send({ type: FT.KEY, key: bobKey.publicKey })
  const bobSeesAlice = await bob.waitFor(FT.PEER_KEY)
  assert.equal(bobSeesAlice.name, 'alice')
  assert.deepEqual([...bobSeesAlice.key], [...aliceKey.publicKey])
  assert.equal((await alice.waitFor(FT.PEER_KEY)).name, 'bob')

  const secret = 'sadece Bob okuyabilir 🔐'
  const { nonce, box } = seal(sodium, aliceKey.privateKey, bobKey.publicKey, Date.now(), secret)
  alice.send({ type: FT.DM, to: 'BOB', nonce, box }) // names are case-insensitive

  const got = await bob.waitFor(FT.DM_RECV)
  assert.deepEqual([got.name, got.to], ['alice', 'bob'])
  assert.deepEqual([...got.box], [...box], 'the server relays the box unchanged')
  assert.equal(open(sodium, bobKey.privateKey, aliceKey.publicKey, got.nonce, got.box).text, secret)

  const echo = await alice.waitFor(FT.DM_RECV)
  assert.equal(open(sodium, aliceKey.privateKey, bobKey.publicKey, echo.nonce, echo.box).text, secret)
  assert.ok(!serverLog.includes('sadece Bob'), 'the server never sees or logs the text')
})

step('a private message to an unknown user is refused', async () => {
  alice.send({ type: FT.DM, to: 'nobody', nonce: new Uint8Array(24), box: new Uint8Array(20) })
  assert.equal((await alice.expect(FT.ERR)).code, ERR.NO_SUCH_USER)
})

step('quit ends the stream and others see LEAVE', async () => {
  bob.send({ type: FT.QUIT }, true)
  await bob.expect('closed')
  assert.equal((await alice.expect(FT.LEAVE)).name, 'bob')
})

step('a client that stops answering is dropped by the heartbeat', async () => {
  const dave = new TcpClient('dave', { answerPings: false }).connect()
  await dave.expect('established')
  dave.send({ type: FT.NAME, name: 'dave' })
  await dave.waitFor(FT.WELCOME) // after the replayed history
  assert.equal((await alice.expect(FT.JOIN)).name, 'dave')
  // alice answers pings and must survive; dave goes silent and must not.
  const left = await alice.next((IDLE_TIMEOUT_S + 3) * 1000)
  assert.deepEqual([left.type, left.name], [FT.LEAVE, 'dave'])
  await dave.waitFor('closed')
})

step('a client that answers pings stays connected past the timeout', async () => {
  alice.send({ type: FT.MSG, text: 'still here' })
  assert.equal((await alice.expect(FT.CHAT)).text, 'still here')
})

step('older messages come page by page', async () => {
  // 4 chats so far + 60 = 64 in the store.
  for (let i = 1; i <= 60; i++) alice.send({ type: FT.MSG, text: `bulk ${i}` })
  for (let i = 1; i <= 60; i++) await alice.expect(FT.CHAT)

  const frank = new WsClient('frank').connect()
  await frank.expect('established')
  frank.send({ type: FT.NAME, name: 'frank' })
  const recent = []
  for (let i = 0; i < 50; i++) recent.push(await frank.expect(FT.CHAT))
  assert.equal(recent.at(-1).text, 'bulk 60')
  assert.ok(recent.every((m, i) => i === 0 || m.id > recent[i - 1].id), 'oldest first')
  assert.equal((await frank.expect(FT.HISTORY_END)).more, true)
  await frank.expect(FT.WELCOME)

  frank.send({ type: FT.HISTORY_REQ, beforeId: recent[0].id, limit: 50 })
  const older = [await frank.waitFor(FT.CHAT)] // after USERS and PEER_KEY
  let m
  while ((m = await frank.next()).type === FT.CHAT) older.push(m)
  assert.equal(m.type, FT.HISTORY_END)
  assert.deepEqual([m.more, m.flags], [false, FLAG.HISTORY | FLAG.PAGE])
  assert.equal(older.length, 14)
  assert.equal(older[0].text, 'merhaba from the browser 🌍')
  assert.ok(older.every((c) => c.flags === (FLAG.HISTORY | FLAG.PAGE)))
  assert.ok(older.at(-1).id < recent[0].id, 'no overlap with what frank already has')

  frank.send({ type: FT.QUIT }, true)
  await frank.waitFor('closed')
  await alice.waitFor(FT.LEAVE)
})

step('history survives a server restart', async () => {
  alice.close()
  await alice.waitFor('closed')
  await stopServer()
  assert.equal(await startServer(), serverFingerprint, 'same identity after restart')

  const gina = new TcpClient('gina').connect()
  await gina.expect('established')
  gina.send({ type: FT.NAME, name: 'gina' })
  const last = await gina.waitFor(FT.HISTORY_END)
  assert.equal(last.more, true)
  // The very first two messages, asked for by id: same ids, same text.
  gina.send({ type: FT.HISTORY_REQ, beforeId: firstIds[1] + 1, limit: 2 })
  const a = await gina.waitFor(FT.CHAT)
  const b = await gina.expect(FT.CHAT)
  assert.deepEqual([a.id, b.id], firstIds)
  assert.deepEqual([a.text, b.text], ['merhaba from the browser 🌍', 'hi from the terminal'])
  assert.equal((await gina.expect(FT.HISTORY_END)).more, false)
  gina.send({ type: FT.QUIT }, true)
  await gina.waitFor('closed')
})

step('a flooding client is warned, then disconnected', async () => {
  const mallet = new TcpClient('mallet').connect()
  await mallet.expect('established')
  mallet.send({ type: FT.NAME, name: 'mallet' })
  await mallet.waitFor(FT.WELCOME)
  for (let i = 0; i < 3 * BURST; i++) mallet.send({ type: FT.MSG, text: `spam ${i}` })

  let chats = 0
  const errors = []
  for (;;) {
    const m = await mallet.next(5000)
    if (m.type === FT.CHAT) chats++
    else if (m.type === FT.ERR) errors.push(m)
    else if (m.type === 'closed') break
  }
  assert.ok(chats < BURST + 5, `only about a burst gets through (${chats})`)
  assert.equal(errors[0]?.code, ERR.RATE_LIMITED)
  assert.match(errors[0].text, /too fast/)
  // The final "flooding" error may be lost: closing a socket the client is
  // still writing to resets it. The server log shows why it was closed.
  assert.match(serverLog, /kept flooding, disconnecting/)
})

step('a wrong pinned fingerprint is refused', async () => {
  const mallory = new WsClient('mallory', { pin: 'NOT-THE-SERVER' }).connect()
  const e = await mallory.expect('error')
  assert.ok(e.error instanceof SecureError)
  assert.match(e.error.message, /not trusted/)
  mallory.close()
})

/* ---------------- run ---------------- */

let failed = 0
for (const [name, fn] of steps) {
  try {
    await fn()
    console.log(`  PASS ${name}`)
  } catch (e) {
    failed++
    console.log(`  FAIL ${name}\n       ${e.message}`)
    break
  }
}

if (!alice.closed) alice.close()
await stopServer()
rmSync(workDir, { recursive: true, force: true })

if (failed) {
  console.log('\nserver log:\n' + serverLog)
  process.exit(1)
}
console.log(`  ${steps.length} steps passed`)
