// Turns the flat message list into what the chat view draws: date
// separators, system lines, and bubbles grouped like a messaging app
// (consecutive messages from one person within a few minutes share one
// avatar and one name).

export const GROUP_GAP_MS = 5 * 60 * 1000

const dayKey = (ts) => {
  const d = new Date(ts)
  return `${d.getFullYear()}-${d.getMonth()}-${d.getDate()}`
}

export function dayLabel(ts, now = Date.now()) {
  if (dayKey(ts) === dayKey(now)) return 'Today'
  if (dayKey(ts) === dayKey(now - 24 * 60 * 60 * 1000)) return 'Yesterday'
  const d = new Date(ts)
  const sameYear = d.getFullYear() === new Date(now).getFullYear()
  return d.toLocaleDateString(undefined, {
    day: 'numeric', month: 'long', ...(sameYear ? {} : { year: 'numeric' }),
  })
}

const isBubble = (m) => m.kind === 'chat' || m.kind === 'dm'

// Same "conversation lane": same sender, and for DMs the same recipient.
const sameLane = (a, b) => a.kind === b.kind && a.name === b.name && (a.kind !== 'dm' || a.to === b.to)

/**
 * @param messages  [{ id, kind, name?, to?, ts?, at, ... }]
 * @returns [{ type: 'date', key, label } | { type: 'line', m } |
 *           { type: 'bubble', m, first, last }]
 */
export function groupMessages(messages, now = Date.now()) {
  const items = []
  let lastDay = null
  let prev = null // previous bubble item, if the previous item was a bubble

  for (const m of messages) {
    const when = m.ts ?? m.at ?? now
    const day = dayKey(when)
    if (day !== lastDay) {
      items.push({ type: 'date', key: `date-${m.id}`, label: dayLabel(when, now) })
      lastDay = day
      prev = null
    }

    if (!isBubble(m)) {
      items.push({ type: 'line', m })
      prev = null
      continue
    }

    const continues = prev && sameLane(prev.m, m) && when - (prev.m.ts ?? 0) <= GROUP_GAP_MS &&
                      Boolean(prev.m.history) === Boolean(m.history)
    if (continues) prev.last = false
    const item = { type: 'bubble', m, first: !continues, last: true }
    items.push(item)
    prev = item
  }
  return items
}
