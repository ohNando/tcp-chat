import { useEffect, useRef } from 'react'

import './EmojiPicker.css'

const GROUPS = [
  ['Smileys', '😀 😃 😄 😁 😆 😅 😂 🤣 😊 😇 🙂 😉 😍 🥰 😘 😋 😜 🤪 😎 🤩 🥳 😏 😌 🤔 🤨 😐 😴 🥱 😮 😲 😳 🥺 😢 😭 😤 😡 🤯 😱 🤗 🤭 🙄 😬'],
  ['Gestures', '👍 👎 👌 ✌️ 🤞 🤟 🤘 👏 🙌 👐 🤝 🙏 💪 👋 🤙 ☝️ 👉 👈 👆 👇 ✋ 🫶'],
  ['Hearts', '❤️ 🧡 💛 💚 💙 💜 🖤 🤍 💔 💕 💖 💯 ✨ 🔥 ⭐ 🌟 ⚡ 🎉 🎊 🎈'],
  ['Things', '☕ 🍕 🍔 🍟 🍰 🎂 🍿 🍺 🍷 ⚽ 🏀 🎮 🎧 🎵 📷 💻 📱 💡 📌 🔒 🔑 ⏰ 📅 ✅ ❌ ⚠️ 🚀 🌍 🌙 ☀️ 🌧️ 🌈 🐱 🐶 🌸 🌳'],
].map(([name, list]) => [name, list.split(' ')])

/** Popover with common emoji; `onPick(emoji)` inserts it. */
export default function EmojiPicker({ onPick, onClose }) {
  const box = useRef(null)

  useEffect(() => {
    const onDown = (e) => { if (box.current && !box.current.contains(e.target)) onClose() }
    const onKey = (e) => { if (e.key === 'Escape') onClose() }
    // Defer so the click that opened the picker does not close it.
    const t = setTimeout(() => document.addEventListener('mousedown', onDown))
    window.addEventListener('keydown', onKey)
    return () => {
      clearTimeout(t)
      document.removeEventListener('mousedown', onDown)
      window.removeEventListener('keydown', onKey)
    }
  }, [onClose])

  return (
    <div className="emoji-picker" ref={box} role="dialog" aria-label="Emoji">
      {GROUPS.map(([name, list]) => (
        <div key={name} className="emoji-group">
          <h4>{name}</h4>
          <div className="emoji-grid">
            {list.map((e) => (
              <button key={e} type="button" className="emoji" onClick={() => onPick(e)}
                      aria-label={e}>{e}</button>
            ))}
          </div>
        </div>
      ))}
    </div>
  )
}
