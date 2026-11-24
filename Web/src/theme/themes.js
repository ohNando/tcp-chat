// Built-in color themes. The desktop app has the same palettes in
// App/src/theme.cpp: keep them in sync when changing a color here.

const STATUS = { danger: '#ef4444', ok: '#22c55e', warn: '#f59e0b' }

export const THEMES = Object.freeze({
  midnight: {
    name: 'Midnight', dark: true,
    bg: '#0b1220', panel: '#111a2b', panelAlt: '#0e1626', border: '#1f2a3d',
    text: '#e6ebf5', muted: '#8a96ab', accent: '#4f8cff',
    ownBubble: '#2b5fd9', otherBubble: '#1a2436', dm: '#a78bfa',
  },
  daylight: {
    name: 'Daylight', dark: false,
    bg: '#e9edf3', panel: '#ffffff', panelAlt: '#f4f6fa', border: '#dde2ea',
    text: '#1b2333', muted: '#6b7280', accent: '#2563eb',
    ownBubble: '#3b82f6', otherBubble: '#ffffff', dm: '#7c3aed',
  },
  ocean: {
    name: 'Ocean', dark: true,
    bg: '#061a1f', panel: '#0b252c', panelAlt: '#082027', border: '#12363f',
    text: '#e2f4f6', muted: '#7fa7ad', accent: '#14b8a6',
    ownBubble: '#0f766e', otherBubble: '#10313a', dm: '#c084fc',
  },
  forest: {
    name: 'Forest', dark: true,
    bg: '#0c140e', panel: '#121d15', panelAlt: '#0f1912', border: '#1e2e22',
    text: '#e5efe6', muted: '#8aa18f', accent: '#4ade80',
    ownBubble: '#166534', otherBubble: '#1a291e', dm: '#c4b5fd',
  },
  sunset: {
    name: 'Sunset', dark: false,
    bg: '#fde9dc', panel: '#fffaf5', panelAlt: '#fff1e6', border: '#f3d9c8',
    text: '#3b2a22', muted: '#9a7b6c', accent: '#f97316',
    ownBubble: '#e11d48', otherBubble: '#ffffff', dm: '#8b5cf6',
  },
  lavender: {
    name: 'Lavender', dark: true,
    bg: '#140f1f', panel: '#1c1530', panelAlt: '#181228', border: '#2c2346',
    text: '#ede9fe', muted: '#a59cc4', accent: '#a78bfa',
    ownBubble: '#7c3aed', otherBubble: '#251c3d', dm: '#f0abfc',
  },
})

export const DEFAULT_DARK = 'midnight'
export const DEFAULT_LIGHT = 'daylight'

function parseHex(hex) {
  const m = /^#?([0-9a-f]{6})$/i.exec(hex)
  if (!m) return null
  const n = parseInt(m[1], 16)
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255]
}

const toHex = (rgb) => '#' + rgb.map((v) => Math.round(Math.min(255, Math.max(0, v)))
  .toString(16).padStart(2, '0')).join('')

/** WCAG relative luminance, 0 (black) .. 1 (white). */
export function luminance(hex) {
  const rgb = parseHex(hex)
  if (!rgb) return 0
  const [r, g, b] = rgb.map((v) => {
    const c = v / 255
    return c <= 0.03928 ? c / 12.92 : ((c + 0.055) / 1.055) ** 2.4
  })
  return 0.2126 * r + 0.7152 * g + 0.0722 * b
}

/** Black or white, whichever is more readable on `bg`. */
export function readableText(bg) {
  return luminance(bg) > 0.4 ? '#111827' : '#ffffff'
}

/** Mixes `hex` toward black (amount > 0) or white (amount < 0). */
export function shade(hex, amount) {
  const rgb = parseHex(hex)
  if (!rgb) return hex
  const target = amount > 0 ? 0 : 255
  const t = Math.abs(amount)
  return toHex(rgb.map((v) => v + (target - v) * t))
}

/** `hex` with alpha 0..1 as an rgba() string. */
export function alpha(hex, a) {
  const rgb = parseHex(hex) ?? [0, 0, 0]
  return `rgba(${rgb.join(', ')}, ${a})`
}

/**
 * Final palette for a theme id plus the user's overrides
 * ({ accent, ownBubble, otherBubble }; unset fields keep the theme's).
 */
export function resolveTheme(id, overrides = {}) {
  const base = THEMES[id] ?? THEMES[DEFAULT_DARK]
  const t = { ...base, ...STATUS }
  for (const key of ['accent', 'ownBubble', 'otherBubble'])
    if (overrides[key] && parseHex(overrides[key])) t[key] = overrides[key]
  t.ownText = readableText(t.ownBubble)
  // Light bubble -> dark text; dark bubble -> the theme's own text color on
  // dark themes (softer than pure white), white otherwise.
  t.otherText = luminance(t.otherBubble) > 0.4 ? '#111827' : (base.dark ? t.text : '#ffffff')
  t.accentText = readableText(t.accent)
  t.accentHover = shade(t.accent, base.dark ? -0.12 : 0.12)
  return t
}

/** CSS custom properties for a resolved palette. */
export function themeVars(t) {
  return {
    '--bg': t.bg,
    '--panel': t.panel,
    '--panel-alt': t.panelAlt,
    '--border': t.border,
    '--text': t.text,
    '--muted': t.muted,
    '--accent': t.accent,
    '--accent-hover': t.accentHover,
    '--accent-text': t.accentText,
    '--accent-soft': alpha(t.accent, 0.15),
    '--own-bubble': t.ownBubble,
    '--own-text': t.ownText,
    '--other-bubble': t.otherBubble,
    '--other-text': t.otherText,
    '--dm': t.dm,
    '--danger': t.danger,
    '--ok': t.ok,
    '--warn': t.warn,
    '--scheme': t.dark ? 'dark' : 'light',
  }
}
