// Per-name visuals shared by avatars and sender labels.

export function nameHue(name) {
  let h = 2166136261 // FNV-1a, same idea as name_color() in the desktop app
  for (const ch of new TextEncoder().encode(name)) h = Math.imul(h ^ ch, 16777619) >>> 0
  return h % 360
}

export function nameColor(name) {
  return `hsl(${nameHue(name)} 60% 55%)`
}

export function initial(name) {
  const first = Array.from(name.trim())[0] ?? '?'
  return first.toLocaleUpperCase('tr')
}
