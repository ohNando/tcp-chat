// Tiny WebDriver BiDi driver for headless Firefox, with no dependencies
// (no Selenium, no geckodriver): Firefox speaks BiDi natively over a
// WebSocket when started with --remote-debugging-port.

import { spawn } from 'node:child_process'
import { mkdtempSync, rmSync, writeFileSync } from 'node:fs'
import { tmpdir } from 'node:os'
import { join } from 'node:path'

const sleep = (ms) => new Promise((r) => setTimeout(r, ms))

export async function launchFirefox({ port = 9333 + Math.floor(Math.random() * 500), width = 1100, height = 680 } = {}) {
  const profile = mkdtempSync(join(tmpdir(), 'tcpchat-ff-'))
  const proc = spawn('firefox', ['--headless', '--remote-debugging-port', String(port),
    '-no-remote', '-profile', profile, `--window-size=${width},${height}`], { stdio: 'ignore' })

  let ws = null
  for (let i = 0; i < 150 && !ws; i++) {
    try {
      const sock = new WebSocket(`ws://127.0.0.1:${port}/session`)
      await new Promise((res, rej) => { sock.onopen = res; sock.onerror = rej })
      ws = sock
    } catch {
      await sleep(200)
    }
  }
  if (!ws) {
    proc.kill()
    throw new Error('could not connect to Firefox (is it installed?)')
  }

  let id = 0
  const pending = new Map()
  ws.onmessage = (e) => {
    const msg = JSON.parse(e.data)
    if (!msg.id || !pending.has(msg.id)) return
    const { resolve, reject } = pending.get(msg.id)
    pending.delete(msg.id)
    if (msg.type === 'error') reject(new Error(`${msg.error}: ${msg.message}`))
    else resolve(msg.result)
  }
  const send = (method, params = {}) => new Promise((resolve, reject) => {
    const n = ++id
    pending.set(n, { resolve, reject })
    ws.send(JSON.stringify({ id: n, method, params }))
  })

  await send('session.new', { capabilities: {} })

  // One "page" object per browsing context (tab).
  const makePage = (context) => {
    const page = {
      // navigate can occasionally hang on 'complete', so wait for
      // 'interactive' and cap it; callers wait for the element they need.
      goto: (url) => Promise.race([
        send('browsingContext.navigate', { context, url, wait: 'interactive' }),
        sleep(5000),
      ]),
      eval: async (expression) => {
        const r = await send('script.evaluate', { expression, target: { context }, awaitPromise: true })
        if (r.type === 'exception') throw new Error(`page exception: ${r.exceptionDetails?.text}`)
        return r.result?.value
      },
      // Sets an input's value the way React notices (native setter + event).
      type: (selector, text) => page.eval(`(() => {
        const el = document.querySelector(${JSON.stringify(selector)})
        Object.getOwnPropertyDescriptor(HTMLInputElement.prototype, 'value').set.call(el, ${JSON.stringify(text)})
        el.dispatchEvent(new Event('input', { bubbles: true }))
        return true
      })()`),
      click: (selector) => page.eval(`(document.querySelector(${JSON.stringify(selector)}).click(), true)`),
      waitFor: async (expression, timeoutMs = 8000) => {
        const end = Date.now() + timeoutMs
        while (Date.now() < end) {
          try { if (await page.eval(expression)) return } catch { /* not ready yet */ }
          await sleep(100)
        }
        throw new Error(`timed out waiting for: ${expression}`)
      },
      screenshot: async (path) => {
        const r = await send('browsingContext.captureScreenshot', { context })
        writeFileSync(path, Buffer.from(r.data, 'base64'))
      },
      // Opens another tab in the same browser (shares localStorage).
      newPage: async () => makePage((await send('browsingContext.create', { type: 'tab' })).context),
      close: async () => {
        // session.end may never answer once Firefox starts shutting down.
        await Promise.race([send('session.end').catch(() => {}), sleep(500)])
        ws.close()
        proc.kill()
        await sleep(200)
        rmSync(profile, { recursive: true, force: true })
      },
    }
    return page
  }

  const page = makePage((await send('browsingContext.getTree', {})).contexts[0].context)
  return page
}
