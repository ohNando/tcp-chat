// Browser smoke test: the real C server serves the built web client, and
// headless Firefox loads it, joins the chat and sends a message.
//
//   npm run build && npm run ui
//   SCREENSHOT=chat.png npm run ui     # also save a screenshot
//
// Needs Firefox and ../Backend/bin/server (run `make` in Backend).

import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { existsSync, mkdtempSync, rmSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'

import { launchFirefox } from './firefox.mjs'

const here = dirname(fileURLToPath(import.meta.url))
const SERVER = join(here, '../../Backend/bin/server')
const DIST = join(here, '../dist')
const WS_PORT = 19080 + Math.floor(Math.random() * 1000)

if (!existsSync(join(DIST, 'index.html'))) {
  console.error('dist/ is missing: run `npm run build` first')
  process.exit(2)
}

const work = mkdtempSync(join(tmpdir(), 'tcpchat-ui-'))
const server = spawn(SERVER, ['-p', String(WS_PORT + 1000), '-w', String(WS_PORT), '-d', DIST],
                     { cwd: work })
let log = ''
server.stdout.on('data', (d) => { log += d })
await new Promise((resolve) => setTimeout(resolve, 400))

const ff = await launchFirefox()
let failed = false
const step = async (name, fn) => {
  if (failed) return
  try {
    await fn()
    console.log(`  PASS ${name}`)
  } catch (e) {
    failed = true
    console.log(`  FAIL ${name}\n       ${e.message}`)
  }
}

await step('page loads and completes the secure handshake', async () => {
  await ff.goto(`http://127.0.0.1:${WS_PORT}/`)
  await ff.waitFor(`document.querySelector('.status')?.textContent.includes('Secure connection')`)
})

await step('the large libsodium bundle was served compressed', async () => {
  assert.match(log, /GET \/assets\/libsodium[^ ]*\.js 200 \(\d+ bytes, (br|gzip)\)/)
})

await step('joins the chat with a Turkish name', async () => {
  await ff.type('.login-screen input', 'Ayşe')
  await ff.click('.login-screen button')
  await ff.waitFor(`document.querySelector('.status')?.textContent.includes('Online')`)
  assert.equal(await ff.eval(`document.querySelector('.user-list li')?.textContent`), 'Ayşe (you)')
})

await step('a sent message comes back from the server', async () => {
  await ff.type('.input-form input', 'Merhaba <b>dünya</b>')
  await ff.click('.input-form button')
  await ff.waitFor(`[...document.querySelectorAll('.message.own')].some(e => e.textContent.includes('dünya'))`)
  // Rendered as text, never as HTML.
  assert.equal(await ff.eval(`!!document.querySelector('.message b')`), false)
})

if (process.env.SCREENSHOT && !failed) await ff.screenshot(process.env.SCREENSHOT)

await ff.close()
server.kill('SIGINT')
await new Promise((resolve) => server.on('exit', resolve))
rmSync(work, { recursive: true, force: true })

if (failed) {
  console.log('\nserver log:\n' + log)
  process.exit(1)
}
console.log('  browser smoke test passed')
