// Browser smoke test: the real C server serves the built web client, and
// headless Firefox loads it, joins the chat and sends a message.
//
//   npm run build && npm run ui
//   SCREENSHOT=chat.png npm run ui     # also save a screenshot
//
// Needs Firefox and ../Backend/bin/server (run `make` in Backend).

import assert from 'node:assert/strict'
import { spawn } from 'node:child_process'
import { existsSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs'
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
  await ff.click('.login-screen button[type=submit]')
  await ff.waitFor(`document.querySelector('.status')?.textContent.includes('Online')`)
  assert.equal(await ff.eval(`document.querySelector('.user-list .user-name')?.textContent`), 'Ayşe (you)')
})

await step('a sent message comes back from the server', async () => {
  await ff.type('.input-form input', 'Merhaba <b>dünya</b>')
  await ff.click('.input-form button[type=submit]')
  await ff.waitFor(`[...document.querySelectorAll('.msg-row.own .bubble')].some(e => e.textContent.includes('dünya'))`)
  // Rendered as text, never as HTML.
  assert.equal(await ff.eval(`!!document.querySelector('.bubble b')`), false)
})

const textOf = (sel) => `(document.querySelector('${sel}')?.textContent ?? '')`
let other
await step('a second tab sees "is typing" while the first one types', async () => {
  other = await ff.newPage()
  await other.goto(`http://127.0.0.1:${WS_PORT}/`)
  await other.waitFor(`${textOf('.status')}.includes('Secure connection')`)
  await other.type('.login-screen input', 'Mehmet')
  await other.click('.login-screen button[type=submit]')
  await other.waitFor(`${textOf('.status')}.includes('Online')`)

  await ff.type('.input-form input', 'yazıyorum')
  await other.waitFor(`${textOf('.typing')}.includes('Ayşe is typing')`)
})

await step('the indicator disappears when the message is sent', async () => {
  await ff.click('.input-form button[type=submit]')
  await other.waitFor(`[...document.querySelectorAll('.msg-row.other .bubble')].some(e => e.textContent.includes('yazıyorum'))`)
  await other.waitFor(`!${textOf('.typing')}.includes('typing')`)
})

await step('a private message reaches only its recipient, marked as encrypted', async () => {
  // Mehmet picks Ayşe in the user list and writes to her.
  await other.waitFor(`!!document.querySelector('button.user-row')`)
  await other.click('button.user-row')
  await other.waitFor(`${textOf('.dm-banner')}.includes('Ayşe')`)
  await other.type('.input-form input', 'sadece sana')
  await other.click('.input-form button[type=submit]')

  await ff.waitFor(`[...document.querySelectorAll('.bubble.dm')].some(e =>
    e.textContent.includes('sadece sana') && e.textContent.includes('Private from Mehmet'))`)
  await other.waitFor(`[...document.querySelectorAll('.msg-row.own .bubble.dm')].some(e =>
    e.textContent.includes('Private to Ayşe'))`)
  assert.ok(!log.includes('sadece sana'), 'the server must not see the text')
})

await step('both sides show the same safety number', async () => {
  await ff.click('button.user-row')
  await ff.waitFor(`!!document.querySelector('.dm-banner code')`)
  const a = await ff.eval(`document.querySelector('.dm-banner code').textContent`)
  const b = await other.eval(`document.querySelector('.dm-banner code').textContent`)
  assert.match(a, /^(\d{5} ){7}\d{5}$/)
  assert.equal(a, b)
})

const cssVar = (page, name) => page.eval(`getComputedStyle(document.documentElement).getPropertyValue('${name}').trim()`)
const closeSettings = (page) => page.eval(`document.querySelector('.settings-backdrop')?.click(), true`)

await step('the emoji picker inserts at the cursor', async () => {
  await ff.type('.input-form input', 'selam ')
  await ff.click('.emoji-button')
  await ff.waitFor(`!!document.querySelector('.emoji-picker')`)
  await ff.click('.emoji-picker button.emoji')
  assert.equal(await ff.eval(`document.querySelector('.input-form input').value`), 'selam 😀')
})

await step('a theme picked in the settings is applied', async () => {
  await ff.click('.settings-button')
  await ff.waitFor(`!!document.querySelector('.settings-panel')`)
  await ff.eval(`[...document.querySelectorAll('.theme-card')].find(c => c.textContent.includes('Ocean')).click(), true`)
  assert.equal(await cssVar(ff, '--accent'), '#14b8a6')
  assert.equal(await ff.eval(`document.documentElement.dataset.theme`), 'ocean')
})

await step('a built-in wallpaper and an uploaded image become the background', async () => {
  await ff.eval(`[...document.querySelectorAll('.wallpaper-swatch')].find(c => c.title === 'Dots').click(), true`)
  await ff.waitFor(`!!document.querySelector('.chat-area.has-wallpaper .wallpaper')`)

  // A tiny 2x2 PNG to upload.
  const png = join(work, 'wall.png')
  writeFileSync(png, Buffer.from('iVBORw0KGgoAAAANSUhEUgAAAAIAAAACCAIAAAD91JpzAAAAFklEQVR4nGP4z8DwHwyBNAMDAwMDAwD1mAj4dW9y0QAAAABJRU5ErkJggg==', 'base64'))
  await ff.setFiles('.wallpaper-actions input[type=file]', [png])
  await ff.waitFor(`(document.querySelector('.wallpaper')?.style.background ?? '').includes('data:image/jpeg')`)
})

await step('text size and compact mode are applied', async () => {
  await ff.eval(`document.querySelector('.segmented button:last-child').click(), true`)
  await ff.eval(`document.querySelectorAll('.settings-panel input.toggle')[0].click(), true`)
  assert.equal(await ff.eval(`document.documentElement.dataset.fontSize`), 'large')
  assert.equal(await ff.eval(`document.documentElement.dataset.compact`), 'true')
  await closeSettings(ff)
})

if (process.env.SCREENSHOT && !failed) await ff.screenshot(process.env.SCREENSHOT)

await step('settings survive a reload', async () => {
  await other.goto(`http://127.0.0.1:${WS_PORT}/`) // the second tab shares localStorage
  await other.waitFor(`document.documentElement.dataset.theme === 'ocean'`)
  assert.equal(await other.eval(`document.documentElement.dataset.fontSize`), 'large')
  const saved = await other.eval(`JSON.parse(localStorage.getItem('tcpchat.settings')).wallpaper`)
  assert.equal(saved, 'custom')
})

await ff.close()
server.kill('SIGINT')
await new Promise((resolve) => server.on('exit', resolve))
rmSync(work, { recursive: true, force: true })

if (failed) {
  console.log('\nserver log:\n' + log)
  process.exit(1)
}
console.log('  browser smoke test passed')
