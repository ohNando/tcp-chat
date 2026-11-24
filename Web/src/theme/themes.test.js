import { test } from 'node:test'
import assert from 'node:assert/strict'

import { THEMES, luminance, readableText, resolveTheme, shade, themeVars } from './themes.js'

const FIELDS = ['bg', 'panel', 'panelAlt', 'border', 'text', 'muted', 'accent', 'ownBubble', 'otherBubble', 'dm']

// WCAG contrast ratio between two colors.
const contrast = (a, b) => {
  const [x, y] = [luminance(a), luminance(b)].sort((p, q) => q - p)
  return (x + 0.05) / (y + 0.05)
}

test('every theme defines every color', () => {
  for (const [id, t] of Object.entries(THEMES))
    for (const f of FIELDS) assert.match(t[f], /^#[0-9a-f]{6}$/i, `${id}.${f}`)
})

test('text stays readable in every theme', () => {
  for (const [id] of Object.entries(THEMES)) {
    const t = resolveTheme(id)
    assert.ok(contrast(t.text, t.panel) >= 7, `${id}: text on panel`)
    assert.ok(contrast(t.ownText, t.ownBubble) >= 3, `${id}: own bubble`)
    assert.ok(contrast(t.otherText, t.otherBubble) >= 4.5, `${id}: other bubble`)
  }
})

test('readable text flips with the background', () => {
  assert.equal(readableText('#ffffff'), '#111827')
  assert.equal(readableText('#000000'), '#ffffff')
  assert.equal(readableText('#fde68a'), '#111827')
})

test('user overrides replace theme colors and keep text readable', () => {
  const t = resolveTheme('midnight', { accent: '#ff0000', ownBubble: '#fef08a', otherBubble: 'nonsense' })
  assert.equal(t.accent, '#ff0000')
  assert.equal(t.ownBubble, '#fef08a')
  assert.equal(t.ownText, '#111827')
  assert.equal(t.otherBubble, THEMES.midnight.otherBubble)
  assert.equal(themeVars(t)['--accent'], '#ff0000')
})

test('unknown theme falls back to the default', () => {
  assert.equal(resolveTheme('nope').bg, THEMES.midnight.bg)
  assert.equal(shade('#ffffff', 1), '#000000')
})
