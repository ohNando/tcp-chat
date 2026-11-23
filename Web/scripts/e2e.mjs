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

import { FT, ERR, encodeMessage, decodeMessage } from '../src/protocol/frame.js'
import { SecureChannel, SecureError, fingerprint } from '../src/protocol/secure.js'

const here = dirname(fileURLToPath(import.meta.url))
const SERVER = join(here, '../../Backend/bin/server')
const TCP_PORT = 18080 + Math.floor(Math.random() * 1000)
const WS_PORT = TCP_PORT + 1000

await sodiumModule.ready
const sodium = sodiumModule

/* ---------------- server process ---------------- */

const workDir = mkdtempSync(join(tmpdir(), 'tcpchat-e2e-'))
const server = spawn(SERVER, ['-p', String(TCP_PORT), '-w', String(WS_PORT)], { cwd: workDir })
let serverLog = ''
const serverFingerprint = await new Promise((resolve, reject) => {
  server.on('error', reject)
  server.stdout.on('data', (d) => {
    serverLog += d
    const fp = serverLog.match(/server fingerprint: (\S+)/)
    if (fp && serverLog.includes('http/websocket on port')) resolve(fp[1])
  })
  setTimeout(() => reject(new Error('server did not start:\n' + serverLog)), 5000)
})

/* ---------------- client helper ---------------- */

// A chat client over either transport. Received messages are queued so the
// test can await them in order.
class Client {
  constructor(name, { pin = serverFingerprint } = {}) {
    this.name = name
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

  onRecord(rec) {
    try {
      const plain = this.channel.receive(rec)
      if (plain === null) {
        if (this.channel.established) this.push({ type: 'established' })
        return
      }
      this.push(decodeMessage(plain))
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

step('websocket client completes the handshake', async () => {
  alice.connect()
  await alice.expect('established')
})

step('websocket client registers a name', async () => {
  alice.send({ type: FT.NAME, name: 'alice' })
  const w = await alice.expect(FT.WELCOME)
  assert.match(w.text, /Welcome, alice/)
  assert.deepEqual((await alice.expect(FT.USERS)).users, ['alice'])
})

step('tcp client joins the same room', async () => {
  bob.connect()
  await bob.expect('established')
  bob.send({ type: FT.NAME, name: 'bob' })
  await bob.expect(FT.WELCOME)
  assert.deepEqual((await bob.expect(FT.USERS)).users, ['alice', 'bob'])
  assert.equal((await alice.expect(FT.JOIN)).name, 'bob')
})

step('chat crosses transports and reaches the sender too', async () => {
  alice.send({ type: FT.MSG, text: 'merhaba from the browser 🌍' })
  for (const c of [alice, bob]) {
    const m = await c.expect(FT.CHAT)
    assert.equal(m.name, 'alice')
    assert.equal(m.text, 'merhaba from the browser 🌍')
    assert.ok(Math.abs(Date.now() - m.timestamp) < 5000)
  }
  bob.send({ type: FT.MSG, text: 'hi from the terminal' })
  assert.equal((await alice.expect(FT.CHAT)).text, 'hi from the terminal')
  await bob.expect(FT.CHAT)
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

step('quit ends the stream and others see LEAVE', async () => {
  bob.send({ type: FT.QUIT }, true)
  await bob.expect('closed')
  assert.equal((await alice.expect(FT.LEAVE)).name, 'bob')
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

alice.close()
server.kill('SIGINT')
await new Promise((r) => server.on('exit', r))
rmSync(workDir, { recursive: true, force: true })

if (failed) {
  console.log('\nserver log:\n' + serverLog)
  process.exit(1)
}
console.log(`  ${steps.length} steps passed`)
