import { useCallback, useEffect, useRef, useState } from 'react'

import { FT, FLAG, encodeMessage, decodeMessage } from '../protocol/frame.js'
import { SecureChannel, fingerprint } from '../protocol/secure.js'
import { Pin, PEER_PINS, checkPin, forgetPin, pinnedFingerprint } from '../protocol/pins.js'
import { loadOrCreateUserKey, seal, open, safetyNumber } from '../protocol/dm.js'

export const Status = Object.freeze({
  CONNECTING: 'connecting',   // socket opening / handshake running
  READY: 'ready',             // secure channel up, no name yet
  REGISTERING: 'registering', // NAME sent, waiting for WELCOME or ERR
  CHATTING: 'chatting',
  DISCONNECTED: 'disconnected',
  UNTRUSTED: 'untrusted',     // pinned fingerprint changed: refuse
})

const MAX_MESSAGES = 500
const TYPING_RESEND_MS = 2000 // refresh our own indicator at most this often
const TYPING_SHOW_MS = 5000   // hide someone else's indicator after this
const RECONNECT_MIN_MS = 1000
const RECONNECT_MAX_MS = 15000
let nextId = 1

// Copy of `obj` without `key` (for the typing map).
function without(obj, key) {
  if (!(key in obj)) return obj
  const copy = { ...obj }
  delete copy[key]
  return copy
}

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
  const [typing, setTyping] = useState({}) // name -> expiry time (ms)
  const [keyedPeers, setKeyedPeers] = useState([]) // members we can DM

  const conn = useRef(null) // { ws, channel, seq }
  const meRef = useRef('')
  // Chats already on screen, so history replayed after a reconnect does
  // not show them twice. Timestamps alone are not unique (two messages can
  // share a millisecond), hence the full key.
  const seenChats = useRef(new Set())
  const typingSent = useRef({ active: false, at: 0 })
  // End-to-end encrypted DMs: our key pair and the members' public keys.
  const sodiumRef = useRef(null)
  const userKey = useRef(null)
  const peers = useRef(new Map())

  const addMessage = useCallback((m) => {
    // `at`: when we received it, used for date separators of system lines.
    setMessages((prev) => [...prev.slice(-(MAX_MESSAGES - 1)), { id: nextId++, at: Date.now(), ...m }])
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

    const sendRaw = (m) => {
      const c = conn.current
      if (c?.channel.established) c.channel.send(encodeMessage(m, c.seq++))
    }

    const setPeer = (name, key) => {
      if (key) peers.current.set(name, key)
      else peers.current.delete(name)
      setKeyedPeers([...peers.current.keys()])
    }

    const onPeerKey = (m) => {
      const sodium = sodiumRef.current
      const who = `${window.location.host}/${m.name}`
      const hex = sodium.to_hex(m.key)
      if (checkPin(window.localStorage, who, hex, PEER_PINS) === Pin.MISMATCH) {
        addMessage({
          kind: 'error',
          text: `The private-message key of ${m.name} has CHANGED. They may have a new ` +
                'device, or the server is intercepting. Compare the new safety number: ' +
                safetyNumber(sodium, userKey.current.publicKey, m.key),
        })
        forgetPin(window.localStorage, who, PEER_PINS)
        checkPin(window.localStorage, who, hex, PEER_PINS)
      }
      setPeer(m.name, m.key)
    }

    const onDm = (m) => {
      // Our own echo is "to" someone else; their key opens it too.
      const other = m.name === meRef.current ? m.to : m.name
      const key = peers.current.get(other)
      try {
        if (!key) throw new Error('no key')
        const { text } = open(sodiumRef.current, userKey.current.privateKey, key, m.nonce, m.box)
        addMessage({ kind: 'dm', name: m.name, to: m.to, text, ts: m.timestamp,
                     own: m.name === meRef.current })
      } catch {
        addMessage({ kind: 'error', text: `Could not decrypt a private message from ${m.name}.` })
      }
    }

    const handle = (m) => {
      switch (m.type) {
        case FT.WELCOME:
          setStatus(Status.CHATTING)
          setError(null)
          addMessage({ kind: 'system', text: m.text })
          sendRaw({ type: FT.KEY, key: userKey.current.publicKey })
          break
        case FT.PEER_KEY:
          onPeerKey(m)
          break
        case FT.DM_RECV:
          onDm(m)
          break
        case FT.CHAT: {
          const history = (m.flags & FLAG.HISTORY) !== 0
          const key = `${m.timestamp}|${m.name}|${m.text}`
          if (history && seenChats.current.has(key)) break
          seenChats.current.add(key)
          if (!history) setTyping((t) => without(t, m.name))
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
          setPeer(m.name, null)
          setTyping((t) => without(t, m.name))
          addMessage({ kind: 'system', text: `${m.name} left the chat` })
          break
        case FT.USERS:
          setUsers(m.users)
          break
        case FT.USER_TYPING:
          setTyping((t) =>
            m.active ? { ...t, [m.name]: Date.now() + TYPING_SHOW_MS } : without(t, m.name))
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
      sodiumRef.current = sodium
      if (!userKey.current) userKey.current = loadOrCreateUserKey(sodium, window.localStorage)

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
        peers.current.clear()
        setKeyedPeers([])
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

  // Forget indicators that were not refreshed in time (the sender may have
  // closed the tab mid-sentence).
  useEffect(() => {
    const timer = setInterval(() => {
      const now = Date.now()
      setTyping((t) => {
        const live = Object.entries(t).filter(([, expiry]) => expiry > now)
        return live.length === Object.keys(t).length ? t : Object.fromEntries(live)
      })
    }, 1000)
    return () => clearInterval(timer)
  }, [])

  /** Call on every input change: tells the others we are (still) typing. */
  const notifyTyping = useCallback((text) => {
    const sent = typingSent.current
    const now = Date.now()
    if (text.trim()) {
      if (!sent.active || now - sent.at > TYPING_RESEND_MS) {
        if (send({ type: FT.TYPING, active: true })) typingSent.current = { active: true, at: now }
      }
    } else if (sent.active) {
      send({ type: FT.TYPING, active: false })
      typingSent.current = { active: false, at: now }
    }
  }, [send])

  const sendText = useCallback((text) => {
    const ok = send({ type: FT.MSG, text })
    // The message itself ends "typing" for the others; just reset our state.
    if (ok) typingSent.current = { active: false, at: 0 }
    return ok
  }, [send])

  /** Sends an end-to-end encrypted private message. */
  const sendDm = useCallback((to, text) => {
    const key = peers.current.get(to)
    if (!key || !userKey.current) return false
    try {
      const { nonce, box } = seal(sodiumRef.current, userKey.current.privateKey, key, Date.now(), text)
      return send({ type: FT.DM, to, nonce, box })
    } catch (e) {
      setError(e.message)
      return false
    }
  }, [send])

  /** Safety number to compare with `name` out of band. */
  const safetyWith = useCallback((name) => {
    const key = peers.current.get(name)
    if (!key || !userKey.current) return null
    return safetyNumber(sodiumRef.current, userKey.current.publicKey, key)
  }, [])

  const clearError = useCallback(() => setError(null), [])

  // After the user confirmed out of band that the key change is legitimate.
  const trustNewServerKey = useCallback(() => {
    forgetPin(window.localStorage, window.location.host)
    window.location.reload()
  }, [])

  const pinned = pinnedFingerprint(window.localStorage, window.location.host)

  return {
    status, messages, users, me, error, serverFingerprint, pinnedFingerprint: pinned,
    reconnectIn, typing: Object.keys(typing), keyedPeers, login, sendText, sendDm, safetyWith,
    notifyTyping, clearError, trustNewServerKey,
  }
}
