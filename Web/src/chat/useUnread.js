import { useEffect, useRef, useState } from 'react'

const away = () => document.hidden || !document.hasFocus()

/** Messages that deserve a notification: DMs and mentions of our name. */
export function isNotable(m, me) {
  if (m.own || m.history) return false
  if (m.kind === 'dm') return true
  return m.kind === 'chat' && Boolean(me) && m.text.toLocaleLowerCase('tr').includes(me.toLocaleLowerCase('tr'))
}

/**
 * Counts messages that arrive while the page is hidden or unfocused, shows
 * the count in the tab title, and (if enabled and allowed) raises a browser
 * notification for private messages and mentions.
 */
export function useUnread(messages, me, notificationsOn) {
  const [unread, setUnread] = useState(0)
  const lastSeenId = useRef(messages.at(-1)?.id ?? 0)

  useEffect(() => {
    const fresh = messages.filter((m) => m.id > lastSeenId.current)
    if (fresh.length === 0) return
    lastSeenId.current = fresh.at(-1).id
    if (!away()) return

    const incoming = fresh.filter((m) => (m.kind === 'chat' || m.kind === 'dm') && !m.own && !m.history)
    if (incoming.length) setUnread((n) => n + incoming.length)

    if (notificationsOn && 'Notification' in window && Notification.permission === 'granted') {
      for (const m of incoming.filter((x) => isNotable(x, me))) {
        const title = m.kind === 'dm' ? `🔒 ${m.name} (private)` : `${m.name} mentioned you`
        new Notification(title, { body: m.text, tag: `tcpchat-${m.id}` })
      }
    }
  }, [messages, me, notificationsOn])

  useEffect(() => {
    const back = () => { if (!away()) setUnread(0) }
    document.addEventListener('visibilitychange', back)
    window.addEventListener('focus', back)
    return () => {
      document.removeEventListener('visibilitychange', back)
      window.removeEventListener('focus', back)
    }
  }, [])

  useEffect(() => {
    document.title = unread ? `(${unread}) TCP Chat` : 'TCP Chat'
  }, [unread])

  return unread
}
