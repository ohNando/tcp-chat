import { useEffect, useRef, useState } from 'react'

import { Status, useChat } from './hooks/useChat'
import { NAME_MAX_LEN, TEXT_MAX_LEN } from './protocol/frame'
import './App.css'

const STATUS_TEXT = {
  [Status.CONNECTING]: 'Connecting securely…',
  [Status.READY]: 'Secure connection established',
  [Status.REGISTERING]: 'Joining…',
  [Status.CHATTING]: 'Online',
  [Status.DISCONNECTED]: 'Disconnected',
  [Status.UNTRUSTED]: 'Server identity changed',
}

const formatTime = (ts) =>
  new Date(ts).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' })

function Fingerprint({ value }) {
  return <code className="fingerprint">{value ?? '…'}</code>
}

function SecurityWarning({ expected, received, onTrust }) {
  return (
    <div className="security-warning" role="alert">
      <h2>Server identity has changed</h2>
      <p>
        The server presented a different key than the one remembered for this
        address. Someone may be intercepting the connection, or the server was
        reinstalled with a new key. The connection was refused.
      </p>
      <dl>
        <dt>Expected</dt>
        <dd><Fingerprint value={expected} /></dd>
        <dt>Received</dt>
        <dd><Fingerprint value={received} /></dd>
      </dl>
      <p>
        Only continue if the server operator confirms the new fingerprint (it is
        printed when the server starts).
      </p>
      <button type="button" className="danger" onClick={onTrust}>
        I verified it, trust the new key
      </button>
    </div>
  )
}

function LoginScreen({ status, error, fingerprint, onLogin }) {
  const [name, setName] = useState('')
  const canJoin = status === Status.READY && name.trim().length >= 2

  const submit = (e) => {
    e.preventDefault()
    if (canJoin) onLogin(name.trim())
  }

  return (
    <form className="login-screen" onSubmit={submit}>
      <h2>TCP Chat</h2>
      <p className={`status status-${status}`}>{STATUS_TEXT[status]}</p>
      <input
        type="text"
        value={name}
        maxLength={NAME_MAX_LEN}
        onChange={(e) => setName(e.target.value)}
        placeholder="Your name"
        autoFocus
      />
      <button type="submit" disabled={!canJoin}>Join chat</button>
      {error && <p className="error">{error}</p>}
      {fingerprint && (
        <p className="server-key">
          Server key <Fingerprint value={fingerprint} />
        </p>
      )}
    </form>
  )
}

function UserList({ users, me }) {
  return (
    <aside className="user-list">
      <h3>Online · {users.length}</h3>
      <ul>
        {[...users].sort((a, b) => a.localeCompare(b)).map((u) => (
          <li key={u} className={u === me ? 'me' : ''}>
            <span className="dot" />
            {u}
            {u === me && <span className="you"> (you)</span>}
          </li>
        ))}
      </ul>
    </aside>
  )
}

function typingText(names) {
  if (names.length === 0) return '\u00a0' // keep the line's height
  if (names.length === 1) return `${names[0]} is typing…`
  if (names.length === 2) return `${names[0]} and ${names[1]} are typing…`
  return `${names.length} people are typing…`
}

function ChatScreen({ status, me, messages, users, typing, error, reconnectIn,
                      onSend, onTyping, onDismissError }) {
  const [input, setInput] = useState('')
  const endRef = useRef(null)

  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [messages])

  const submit = (e) => {
    e.preventDefault()
    const text = input.trim()
    if (text && onSend(text)) setInput('')
  }

  const online = status === Status.CHATTING

  return (
    <div className="chat-layout">
      <div className="chat-container">
        <header className="header">
          <h1>TCP Chat</h1>
          <p className={`status status-${status}`}>
            {me} · {STATUS_TEXT[status]}
            {status === Status.DISCONNECTED && reconnectIn !== null &&
              ` · retrying in ${Math.ceil(reconnectIn / 1000)}s`}
          </p>
        </header>

        {error && (
          <div className="error-banner" role="alert">
            <span>{error}</span>
            <button type="button" onClick={onDismissError} aria-label="Dismiss">×</button>
          </div>
        )}

        <div className="messages-box">
          {messages.map((m) =>
            m.kind === 'chat' ? (
              <div key={m.id} className={`message ${m.own ? 'own' : 'other'}${m.history ? ' history' : ''}`}>
                {!m.own && <div className="message-sender">{m.name}</div>}
                <div className="message-content">{m.text}</div>
                <div className="message-time">{formatTime(m.ts)}</div>
              </div>
            ) : (
              <div key={m.id} className="message server">{m.text}</div>
            ),
          )}
          <div ref={endRef} />
        </div>
        <div className="typing" aria-live="polite">{typingText(typing)}</div>

        <form onSubmit={submit} className="input-form">
          <input
            type="text"
            value={input}
            maxLength={TEXT_MAX_LEN}
            onChange={(e) => {
              setInput(e.target.value)
              onTyping(e.target.value)
            }}
            placeholder="Type a message…"
            disabled={!online}
          />
          <button type="submit" disabled={!online || !input.trim()}>Send</button>
        </form>
      </div>
      <UserList users={users} me={me} />
    </div>
  )
}

export default function App() {
  const chat = useChat()

  if (chat.status === Status.UNTRUSTED) {
    return (
      <SecurityWarning expected={chat.pinnedFingerprint} received={chat.serverFingerprint}
                       onTrust={chat.trustNewServerKey} />
    )
  }

  // Stay on the chat screen while a dropped connection is being restored.
  const inRoom = Boolean(chat.me) && chat.status !== Status.READY

  return inRoom ? (
    <ChatScreen status={chat.status} me={chat.me} messages={chat.messages} users={chat.users}
                typing={chat.typing} error={chat.error} reconnectIn={chat.reconnectIn}
                onSend={chat.sendText} onTyping={chat.notifyTyping}
                onDismissError={chat.clearError} />
  ) : (
    <LoginScreen status={chat.status} error={chat.error} fingerprint={chat.serverFingerprint}
                 onLogin={chat.login} />
  )
}
