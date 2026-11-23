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

function LoginScreen({ status, error, onLogin }) {
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

function ChatScreen({ status, me, messages, users, reconnectIn, onSend }) {
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

      <div className="messages-box">
        {messages.map((m) =>
          m.kind === 'chat' ? (
            <div key={m.id} className={`message ${m.own ? 'own' : 'other'}`}>
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

      <form onSubmit={submit} className="input-form">
        <input
          type="text"
          value={input}
          maxLength={TEXT_MAX_LEN}
          onChange={(e) => setInput(e.target.value)}
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
  // Stay on the chat screen while a dropped connection is being restored.
  const inRoom = Boolean(chat.me) && chat.status !== Status.READY && chat.status !== Status.UNTRUSTED

  return inRoom ? (
    <ChatScreen status={chat.status} me={chat.me} messages={chat.messages} users={chat.users}
                reconnectIn={chat.reconnectIn} onSend={chat.sendText} />
  ) : (
    <LoginScreen status={chat.status} error={chat.error} onLogin={chat.login} />
  )
}
