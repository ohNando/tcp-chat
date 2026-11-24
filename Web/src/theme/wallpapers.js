// Built-in chat backgrounds, generated from the current theme's colors so
// they always match it. No image files and no network: just CSS.

import { alpha } from './themes.js'

const svg = (body, size) =>
  `url("data:image/svg+xml,${encodeURIComponent(
    `<svg xmlns='http://www.w3.org/2000/svg' width='${size}' height='${size}'>${body}</svg>`)}")`

export const WALLPAPERS = Object.freeze([
  {
    id: 'dots', name: 'Dots',
    css: (t) => `${svg(`<circle cx='4' cy='4' r='1.6' fill='${alpha(t.accent, 0.35)}'/>`, 22)}, ${t.panelAlt}`,
  },
  {
    id: 'grid', name: 'Grid',
    css: (t) => `${svg(`<path d='M32 0H0V32' fill='none' stroke='${alpha(t.muted, 0.18)}'/>`, 32)}, ${t.panelAlt}`,
  },
  {
    id: 'waves', name: 'Waves',
    css: (t) => `${svg(
      `<path d='M0 20 Q15 8 30 20 T60 20' fill='none' stroke='${alpha(t.accent, 0.22)}' stroke-width='2'/>` +
      `<path d='M0 45 Q15 33 30 45 T60 45' fill='none' stroke='${alpha(t.dm, 0.18)}' stroke-width='2'/>`, 60)}, ${t.panelAlt}`,
  },
  {
    id: 'bubbles', name: 'Bubbles',
    css: (t) => `${svg(
      `<circle cx='20' cy='22' r='10' fill='${alpha(t.accent, 0.1)}'/>` +
      `<circle cx='70' cy='60' r='16' fill='${alpha(t.dm, 0.09)}'/>` +
      `<circle cx='95' cy='15' r='6' fill='${alpha(t.ownBubble, 0.14)}'/>` +
      `<circle cx='35' cy='95' r='7' fill='${alpha(t.accent, 0.12)}'/>`, 120)}, ${t.panelAlt}`,
  },
  {
    id: 'aurora', name: 'Aurora',
    css: (t) => `radial-gradient(ellipse at 15% 10%, ${alpha(t.accent, 0.35)}, transparent 55%), ` +
                `radial-gradient(ellipse at 85% 30%, ${alpha(t.dm, 0.3)}, transparent 55%), ` +
                `radial-gradient(ellipse at 50% 100%, ${alpha(t.ownBubble, 0.3)}, transparent 60%), ${t.panelAlt}`,
  },
  {
    id: 'dusk', name: 'Dusk',
    css: () => 'linear-gradient(160deg, #2b1055 0%, #7597de 55%, #f6b38e 100%)',
  },
])

export function wallpaperCss(id, palette, customImage) {
  if (id === 'custom' && customImage) return `url("${customImage}") center / cover no-repeat`
  return WALLPAPERS.find((w) => w.id === id)?.css(palette) ?? null
}

/**
 * Shrinks an image file so it fits in localStorage: at most `max` pixels
 * on the long side, re-encoded as JPEG. Returns a data URL.
 */
export async function shrinkImage(file, max = 1920, quality = 0.85) {
  if (!file.type.startsWith('image/')) throw new Error('Please choose an image file.')
  const bitmap = await createImageBitmap(file)
  const scale = Math.min(1, max / Math.max(bitmap.width, bitmap.height))
  const canvas = document.createElement('canvas')
  canvas.width = Math.round(bitmap.width * scale)
  canvas.height = Math.round(bitmap.height * scale)
  canvas.getContext('2d').drawImage(bitmap, 0, 0, canvas.width, canvas.height)
  bitmap.close()
  return canvas.toDataURL('image/jpeg', quality)
}
