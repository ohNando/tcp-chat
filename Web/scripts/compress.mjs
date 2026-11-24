// Precompresses the production build so the C server can send .br / .gz
// files as they are, without compressing anything at request time.
// Runs after `vite build` (see the "build" script in package.json).

import { readdirSync, readFileSync, statSync, writeFileSync } from 'node:fs'
import { join, extname, dirname } from 'node:path'
import { fileURLToPath } from 'node:url'
import { brotliCompressSync, gzipSync, constants } from 'node:zlib'

const dist = join(dirname(fileURLToPath(import.meta.url)), '..', 'dist')
const TEXT = new Set(['.html', '.js', '.mjs', '.css', '.svg', '.json', '.txt', '.wasm'])
const MIN_SIZE = 1024 // tiny files are not worth it

function* walk(dir) {
  for (const name of readdirSync(dir)) {
    const path = join(dir, name)
    if (statSync(path).isDirectory()) yield* walk(path)
    else yield path
  }
}

let before = 0, br = 0, gz = 0
for (const path of walk(dist)) {
  if (!TEXT.has(extname(path))) continue
  const data = readFileSync(path)
  if (data.length < MIN_SIZE) continue

  const brData = brotliCompressSync(data, {
    params: {
      [constants.BROTLI_PARAM_QUALITY]: constants.BROTLI_MAX_QUALITY,
      [constants.BROTLI_PARAM_SIZE_HINT]: data.length,
    },
  })
  const gzData = gzipSync(data, { level: 9 })
  writeFileSync(path + '.br', brData)
  writeFileSync(path + '.gz', gzData)

  before += data.length
  br += brData.length
  gz += gzData.length
}

const kb = (n) => `${(n / 1024).toFixed(1)} KiB`
console.log(`precompressed ${kb(before)} -> brotli ${kb(br)}, gzip ${kb(gz)}`)
