import { useCallback, useEffect, useRef, useState } from 'react'

import { FT, FLAG, encodeMessage, decodeMessage } from '../protocol/frame.js'
import { SecureChannel, fingerprint } from '../protocol/secure.js'
import { Pin, checkPin, forgetPin, pinnedFingerprint } from '../protocol/pins.js'

export const Status = Object.freeze({
  CONNECTING: 'connecting',   // socket opening / handshake running
  READY: 'ready',             // secure channel up, no name yet
  REGISTERING: 'registering', // NAME sent, waiting for WELCOME or ERR
  CHATTING: 'chatting',
  DISCONNECTED: 'disconnected',
  UNTRUSTED: 'untrusted',     // pinned fingerprint changed: refuse
})

const MAX_MESSAGES = 500
const RECONNECT_MIN_MS = 1000
const RECONNECT_MAX_MS = 15000
let nextId = 1

function wsUrl() {
  const proto = window.location.protocol === 'https:' ? 'wss:' : 'ws:'
  return `${proto}//${window.location.host}/ws`
}

/**
 * Encrypted chat over WebSocket. The server is the single source of truth:
 * our own messages are shown only when the server broadcasts them back.
 */
export function useChat() {
  const [status, setStatus] = useState(Status.CONNECTING)
  const [messages, setMessages] = useState([])
  const [users, setUsers] = useState([])
  const [me, setMe] = useState('')
  const [error, setError] = useState(null)
  const [serverFingerprint, setServerFingerprint] = useState(null)
  const [reconnectIn, setReconnectIn] = useState(null)

  const conn = useRef(null) // { ws, channel, seq }
  const meRef = useRef('')
  // Chats already on screen, so history replayed after a reconnect does
  // not show them twice. Timestamps alone are not unique (two messages can
  // share a millisecond), hence the full key.
  const seenChats = useRef(new Set())

  const addMessage = useCallback((m) => {
    setMessages((prev) => [...prev.slice(-(MAX_MESSAGES - 1)), { id: nextId++, ...m }])
  }, [])

  const send = useCallback((m, final = false) => {
    const c = conn.current
    if (!c?.channel.established) return false
    c.channel.send(encodeMessage(m, c.seq++), final)
    return true
  }, [])

  useEffect(() => {
    let cancelled = false
    let untrusted = false
    let attempt = 0
    let timer = null

    const handle = (m) => {
      switch (m.type) {
        case FT.WELCOME:
          setStatus(Status.CHATTING)
          setError(null)
          addMessage({ kind: 'system', text: m.text })
          break
        case FT.CHAT: {
          const history = (m.flags & FLAG.HISTORY) !== 0
          const key = `${m.timestamp}|${m.name}|${m.text}`
          if (history && seenChats.current.has(key)) break
          seenChats.current.add(key)
          addMessage({ kind: 'chat', name: m.name, text: m.text, ts: m.timestamp,
                       own: m.name === meRef.current, history })
          break
        }
        case FT.JOIN:
          setUsers((u) => (u.includes(m.name) ? u : [...u, m.name]))
          addMessage({ kind: 'system', text: `${m.name} joined the chat` })
          break
        case FT.LEAVE:
          setUsers((u) => u.filter((n) => n !== m.name))
          addMessage({ kind: 'system', text: `${m.name} left the chat` })
          break
        case FT.USERS:
          setUsers(m.users)
          break
        case FT.PING: {
          // Heartbeat: the server drops connections that stay silent.
          const c = conn.current
          if (c?.channel.established) c.channel.send(encodeMessage({ type: FT.PONG }, c.seq++))
          break
        }
        case FT.ERR:
          setError(m.text)
          // A rejected NAME sends us back to the login screen.
          setStatus((s) => {
            if (s !== Status.REGISTERING) return s
            meRef.current = ''
            setMe('')
            return Status.READY
          })
          break
        default:
          break
      }
    }

    const connect = async () => {
      // libsodium is large (~500 KB): load it in its own chunk so the page
      // renders before the crypto code has arrived.
      const sodium = (await import('libsodium-wrappers')).default
      await sodium.ready
      if (cancelled) return

      const ws = new WebSocket(wsUrl())
      ws.binaryType = 'arraybuffer'

      const channel = new SecureChannel(sodium, {
        emit: (rec) => ws.send(rec),
        verify: (pk) => {
          const fp = fingerprint(sodium, pk)
          setServerFingerprint(fp)
          const pin = checkPin(window.localStorage, window.location.host, fp)
          if (pin === Pin.MISMATCH) {
            untrusted = true
            setStatus(Status.UNTRUSTED)
            return false
          }
          if (pin === Pin.NEW)
            addMessage({ kind: 'system', text: `New server, fingerprint saved: ${fp}` })
          return true
        },
      })
      conn.current = { ws, channel, seq: 0 }

      ws.onopen = () => channel.start()

      ws.onmessage = (e) => {
        try {
          const plain = channel.receive(new Uint8Array(e.data))
          if (plain === null) {
            if (!channel.established) return
            attempt = 0
            setReconnectIn(null)
            if (meRef.current) {
              // Reconnected after a drop: rejoin with the same name.
              setStatus(Status.REGISTERING)
              channel.send(encodeMessage({ type: FT.NAME, name: meRef.current }, conn.current.seq++))
            } else {
              setStatus(Status.READY)
            }
            return
          }
          handle(decodeMessage(plain))
        } catch (err) {
          setError(err.message)
          ws.close()
        }
      }

      ws.onclose = () => {
        if (conn.current?.ws === ws) conn.current = null
        setUsers([])
        if (cancelled || untrusted) return
        setStatus(Status.DISCONNECTED)

        // Exponential backoff with jitter: 1s, 2s, 4s ... capped at 15s.
        const delay = Math.min(RECONNECT_MAX_MS, RECONNECT_MIN_MS * 2 ** attempt)
        const jittered = Math.round(delay * (0.75 + Math.random() * 0.5))
        attempt++
        setReconnectIn(jittered)
        timer = setTimeout(() => {
          setStatus(Status.CONNECTING)
          connect()
        }, jittered)
      }
    }

    connect()
    return () => {
      cancelled = true
      clearTimeout(timer)
      conn.current?.ws.close()
    }
  }, [addMessage])

  const login = useCallback((name) => {
    meRef.current = name
    setMe(name)
    setError(null)
    setStatus(Status.REGISTERING)
    send({ type: FT.NAME, name })
  }, [send])

  const sendText = useCallback((text) => send({ type: FT.MSG, text }), [send])

  const clearError = useCallback(() => setError(null), [])

  // After the user confirmed out of band that the key change is legitimate.
  const trustNewServerKey = useCallback(() => {
    forgetPin(window.localStorage, window.location.host)
    window.location.reload()
  }, [])

  const pinned = pinnedFingerprint(window.localStorage, window.location.host)

  return {
    status, messages, users, me, error, serverFingerprint, pinnedFingerprint: pinned,
    reconnectIn, login, sendText, clearError, trustNewServerKey,
  }
}
