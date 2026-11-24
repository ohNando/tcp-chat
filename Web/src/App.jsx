import { useEffect, useRef, useState } from 'react'

import { Status, useChat } from './hooks/useChat'
import { useSettings } from './theme/useSettings'
import { groupMessages } from './chat/groupMessages'
import Avatar from './components/Avatar'
import SettingsPanel from './components/SettingsPanel'
import { nameColor } from './chat/names'
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

function SettingsButton({ onClick }) {
  return (
    <button type="button" className="icon settings-button" onClick={onClick}
            aria-label="Settings" title="Settings">⚙️</button>
  )
}

function LoginScreen({ status, error, fingerprint, onLogin, onOpenSettings }) {
  const [name, setName] = useState('')
  const canJoin = status === Status.READY && name.trim().length >= 2

  const submit = (e) => {
    e.preventDefault()
    if (canJoin) onLogin(name.trim())
  }

  return (
    <form className="login-screen" onSubmit={submit}>
      <div className="login-corner"><SettingsButton onClick={onOpenSettings} /></div>
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

function UserList({ users, me, keyedPeers, dmTarget, onPick }) {
  return (
    <aside className="user-list">
      <h3>Online · {users.length}</h3>
      <ul>
        {[...users].sort((a, b) => a.localeCompare(b)).map((u) => {
          const canDm = u !== me && keyedPeers.includes(u)
          const body = (
            <>
              <span className="user-avatar"><Avatar name={u} size={32} /><span className="online-dot" /></span>
              <span className="user-name">{u}{u === me && <span className="you"> (you)</span>}</span>
              {canDm && <span className="lock" aria-hidden>🔒</span>}
            </>
          )
          return (
            <li key={u} className={u === dmTarget ? 'selected' : ''}>
              {canDm ? (
                <button type="button" className="user-row" title={`Private message to ${u}`}
                        onClick={() => onPick(u === dmTarget ? null : u)}>
                  {body}
                </button>
              ) : (
                <div className="user-row">{body}</div>
              )}
            </li>
          )
        })}
      </ul>
      {keyedPeers.length > 0 && <p className="hint">Click a 🔒 name for an encrypted private message.</p>}
    </aside>
  )
}

function Bubble({ item }) {
  const { m, first, last } = item
  const dm = m.kind === 'dm'
  const showName = first && !m.own && !dm
  const rowClass = ['msg-row', m.own ? 'own' : 'other', first && 'first', last && 'last',
                    m.history && 'history'].filter(Boolean).join(' ')
  return (
    <div className={rowClass}>
      {!m.own && (first ? <Avatar name={m.name} /> : <span className="avatar-spacer" />)}
      <div className={`bubble${dm ? ' dm' : ''}`}>
        {showName && <div className="bubble-name" style={{ color: nameColor(m.name) }}>{m.name}</div>}
        {dm && <div className="bubble-dm">🔒 Private {m.own ? `to ${m.to}` : `from ${m.name}`}</div>}
        <span className="bubble-text">{m.text}</span>
        <span className="bubble-time">{formatTime(m.ts)}</span>
      </div>
    </div>
  )
}

function MessageList({ messages, endRef }) {
  const items = groupMessages(messages)
  return (
    <div className="messages-box">
      {items.map((it) => {
        if (it.type === 'date') return <div key={it.key} className="day-separator"><span>{it.label}</span></div>
        if (it.type === 'line')
          return (
            <div key={it.m.id} className={`system-line${it.m.kind === 'error' ? ' error-line' : ''}`}>
              <span>{it.m.text}</span>
            </div>
          )
        return <Bubble key={it.m.id} item={it} />
      })}
      <div ref={endRef} />
    </div>
  )
}

function typingText(names) {
  if (names.length === 0) return '\u00a0' // keep the line's height
  if (names.length === 1) return `${names[0]} is typing…`
  if (names.length === 2) return `${names[0]} and ${names[1]} are typing…`
  return `${names.length} people are typing…`
}

function ChatScreen({ status, me, messages, users, typing, keyedPeers, error, reconnectIn,
                      onSend, onSendDm, safetyWith, onTyping, onDismissError, onOpenSettings }) {
  const [input, setInput] = useState('')
  const [dmTarget, setDmTarget] = useState(null)
  const endRef = useRef(null)
  // The person may have left (or lost their key) since we picked them.
  const target = dmTarget && keyedPeers.includes(dmTarget) ? dmTarget : null

  useEffect(() => {
    endRef.current?.scrollIntoView({ behavior: 'smooth' })
  }, [messages])

  const submit = (e) => {
    e.preventDefault()
    const text = input.trim()
    if (!text) return
    if (target ? onSendDm(target, text) : onSend(text)) setInput('')
  }

  const online = status === Status.CHATTING

  return (
    <div className="chat-layout">
      <div className="chat-container">
        <header className="header">
          <span className="room-icon" aria-hidden>#</span>
          <div className="header-titles">
            <h1>TCP Chat</h1>
            <p className={`status status-${status}`}>
              {STATUS_TEXT[status]} · {users.length} online · signed in as {me}
              {status === Status.DISCONNECTED && reconnectIn !== null &&
                ` · retrying in ${Math.ceil(reconnectIn / 1000)}s`}
            </p>
          </div>
          <SettingsButton onClick={onOpenSettings} />
        </header>

        {error && (
          <div className="error-banner" role="alert">
            <span>{error}</span>
            <button type="button" onClick={onDismissError} aria-label="Dismiss">×</button>
          </div>
        )}

        <MessageList messages={messages} endRef={endRef} />
        <div className="typing" aria-live="polite">{typingText(typing)}</div>

        {target && (
          <div className="dm-banner">
            <span>
              🔒 Private message to <strong>{target}</strong>, end-to-end encrypted.
              Safety number <code>{safetyWith(target)}</code>
            </span>
            <button type="button" onClick={() => setDmTarget(null)} aria-label="Back to the room">×</button>
          </div>
        )}

        <form onSubmit={submit} className="input-form">
          <input
            type="text"
            value={input}
            maxLength={TEXT_MAX_LEN}
            onChange={(e) => {
              setInput(e.target.value)
              if (!target) onTyping(e.target.value) // never announce typing a DM
            }}
            placeholder={target ? `Private message to ${target}…` : 'Type a message…'}
            disabled={!online}
          />
          <button type="submit" disabled={!online || !input.trim()}>Send</button>
        </form>
      </div>
      <UserList users={users} me={me} keyedPeers={keyedPeers} dmTarget={target} onPick={setDmTarget} />
    </div>
  )
}

export default function App() {
  const chat = useChat()
  const look = useSettings() // also applies the saved theme
  const [settingsOpen, setSettingsOpen] = useState(false)
  const openSettings = () => setSettingsOpen(true)
  const panel = settingsOpen && (
    <SettingsPanel settings={look.settings} update={look.update} palette={look.palette}
                   onClose={() => setSettingsOpen(false)} />
  )

  if (chat.status === Status.UNTRUSTED) {
    return (
      <SecurityWarning expected={chat.pinnedFingerprint} received={chat.serverFingerprint}
                       onTrust={chat.trustNewServerKey} />
    )
  }

  // Stay on the chat screen while a dropped connection is being restored.
  const inRoom = Boolean(chat.me) && chat.status !== Status.READY

  return (
    <>
      {inRoom ? (
        <ChatScreen status={chat.status} me={chat.me} messages={chat.messages} users={chat.users}
                    typing={chat.typing} keyedPeers={chat.keyedPeers} error={chat.error}
                    reconnectIn={chat.reconnectIn} onSend={chat.sendText} onSendDm={chat.sendDm}
                    safetyWith={chat.safetyWith} onTyping={chat.notifyTyping}
                    onDismissError={chat.clearError} onOpenSettings={openSettings} />
      ) : (
        <LoginScreen status={chat.status} error={chat.error} fingerprint={chat.serverFingerprint}
                     onLogin={chat.login} onOpenSettings={openSettings} />
      )}
      {panel}
    </>
  )
}
