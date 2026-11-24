import { useEffect } from 'react'

import { THEMES, resolveTheme } from '../theme/themes.js'
import { systemTheme } from '../theme/useSettings.js'
import './SettingsPanel.css'

function ThemeCard({ id, selected, onPick, label }) {
  // `id` null = follow the system: preview whatever the system picks now.
  const t = resolveTheme(id ?? systemTheme())
  return (
    <button type="button" className={`theme-card${selected ? ' selected' : ''}`}
            onClick={() => onPick(id)} aria-pressed={selected}>
      <span className="theme-preview" style={{ background: t.panelAlt, borderColor: t.border }}>
        <span className="preview-bubble other" style={{ background: t.otherBubble }} />
        <span className="preview-bubble own" style={{ background: t.ownBubble }} />
        <span className="preview-accent" style={{ background: t.accent }} />
      </span>
      <span className="theme-name">{label}</span>
    </button>
  )
}

function ColorRow({ label, value, fallback, onChange }) {
  return (
    <label className="color-row">
      <span>{label}</span>
      <input type="color" value={value ?? fallback} onChange={(e) => onChange(e.target.value)} />
      <button type="button" className="link" disabled={!value} onClick={() => onChange(null)}>
        Reset
      </button>
    </label>
  )
}

export default function SettingsPanel({ settings, update, palette, onClose }) {
  useEffect(() => {
    const onKey = (e) => { if (e.key === 'Escape') onClose() }
    window.addEventListener('keydown', onKey)
    return () => window.removeEventListener('keydown', onKey)
  }, [onClose])

  return (
    <div className="settings-backdrop" onClick={onClose}>
      <aside className="settings-panel" role="dialog" aria-label="Settings"
             onClick={(e) => e.stopPropagation()}>
        <header>
          <h2>Appearance</h2>
          <button type="button" className="icon" onClick={onClose} aria-label="Close settings">×</button>
        </header>

        <section>
          <h3>Theme</h3>
          <div className="theme-grid">
            <ThemeCard id={null} label="Match system" selected={!settings.theme}
                       onPick={(theme) => update({ theme })} />
            {Object.entries(THEMES).map(([id, t]) => (
              <ThemeCard key={id} id={id} label={t.name} selected={settings.theme === id}
                         onPick={(theme) => update({ theme })} />
            ))}
          </div>
        </section>

        <section>
          <h3>Colors</h3>
          <ColorRow label="Accent" value={settings.accent} fallback={palette.accent}
                    onChange={(accent) => update({ accent })} />
          <ColorRow label="My messages" value={settings.ownBubble} fallback={palette.ownBubble}
                    onChange={(ownBubble) => update({ ownBubble })} />
          <ColorRow label="Their messages" value={settings.otherBubble} fallback={palette.otherBubble}
                    onChange={(otherBubble) => update({ otherBubble })} />
          <p className="note">Text colors adjust automatically so messages stay readable.</p>
        </section>
      </aside>
    </div>
  )
}
