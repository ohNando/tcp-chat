import { useEffect, useRef, useState } from 'react'

import { THEMES, resolveTheme } from '../theme/themes.js'
import { systemTheme } from '../theme/useSettings.js'
import { WALLPAPERS, shrinkImage } from '../theme/wallpapers.js'
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

function WallpaperSwatch({ label, background, selected, onPick }) {
  return (
    <button type="button" className={`wallpaper-swatch${selected ? ' selected' : ''}`}
            onClick={onPick} aria-pressed={selected} title={label}>
      <span className="swatch" style={{ background }} />
      <span>{label}</span>
    </button>
  )
}

function Slider({ label, value, min, max, step, format, onChange }) {
  return (
    <label className="slider-row">
      <span>{label}</span>
      <input type="range" min={min} max={max} step={step} value={value}
             onChange={(e) => onChange(Number(e.target.value))} />
      <output>{format(value)}</output>
    </label>
  )
}

function Background({ settings, update, palette, customWallpaper, setCustomWallpaper }) {
  const fileInput = useRef(null)
  const [error, setError] = useState(null)

  const upload = async (file) => {
    if (!file) return
    setError(null)
    try {
      setCustomWallpaper(await shrinkImage(file))
      update({ wallpaper: 'custom' })
    } catch (e) {
      setError(e.name === 'QuotaExceededError'
        ? 'This image is too large for the browser storage. Try a smaller one.'
        : e.message)
    }
  }

  return (
    <section>
      <h3>Chat background</h3>
      <div className="wallpaper-grid">
        <WallpaperSwatch label="None" background={palette.panelAlt}
                         selected={settings.wallpaper === 'none'} onPick={() => update({ wallpaper: 'none' })} />
        {WALLPAPERS.map((w) => (
          <WallpaperSwatch key={w.id} label={w.name} background={w.css(palette)}
                           selected={settings.wallpaper === w.id} onPick={() => update({ wallpaper: w.id })} />
        ))}
        {customWallpaper && (
          <WallpaperSwatch label="My image" background={`url("${customWallpaper}") center / cover`}
                           selected={settings.wallpaper === 'custom'}
                           onPick={() => update({ wallpaper: 'custom' })} />
        )}
      </div>
      <div className="wallpaper-actions">
        <button type="button" onClick={() => fileInput.current.click()}>Upload image…</button>
        {customWallpaper && (
          <button type="button" className="link" onClick={() => {
            setCustomWallpaper(null)
            if (settings.wallpaper === 'custom') update({ wallpaper: 'none' })
          }}>Remove my image</button>
        )}
        <input ref={fileInput} type="file" accept="image/*" hidden
               onChange={(e) => { upload(e.target.files[0]); e.target.value = '' }} />
      </div>
      {error && <p className="error">{error}</p>}
      {settings.wallpaper !== 'none' && (
        <>
          <Slider label="Dim" value={settings.dim} min={0} max={0.8} step={0.05}
                  format={(v) => `${Math.round(v * 100)}%`} onChange={(dim) => update({ dim })} />
          <Slider label="Blur" value={settings.blur} min={0} max={20} step={1}
                  format={(v) => `${v}px`} onChange={(blur) => update({ blur })} />
        </>
      )}
    </section>
  )
}

function Segmented({ options, value, onChange, label }) {
  return (
    <div className="segmented" role="radiogroup" aria-label={label}>
      {options.map(([id, text]) => (
        <button key={id} type="button" role="radio" aria-checked={value === id}
                className={value === id ? 'on' : ''} onClick={() => onChange(id)}>
          {text}
        </button>
      ))}
    </div>
  )
}

function Notifications({ settings, update }) {
  const supported = 'Notification' in window
  const [denied, setDenied] = useState(supported && Notification.permission === 'denied')

  const toggle = async (on) => {
    if (on && supported && Notification.permission !== 'granted') {
      const result = await Notification.requestPermission()
      setDenied(result === 'denied')
      if (result !== 'granted') return
    }
    update({ notifications: on })
  }

  return (
    <section>
      <h3>Notifications</h3>
      <label className="setting-row">
        <span>Notify me about private messages and mentions</span>
        <input type="checkbox" className="toggle" disabled={!supported}
               checked={settings.notifications && !denied} onChange={(e) => toggle(e.target.checked)} />
      </label>
      <p className="note">
        {!supported ? 'This browser does not support notifications.'
          : denied ? 'Notifications are blocked for this site in your browser settings.'
          : 'The tab title always shows how many messages you missed.'}
      </p>
    </section>
  )
}

function Connection({ fingerprint }) {
  if (!fingerprint) return null
  return (
    <section>
      <h3>Connection</h3>
      <p className="setting-label">Server fingerprint</p>
      <code className="fingerprint">{fingerprint}</code>
      <p className="note">
        Remembered the first time you connected. If it ever changes, TCP Chat
        warns you and does not connect.
      </p>
    </section>
  )
}

export default function SettingsPanel({ settings, update, palette, customWallpaper, setCustomWallpaper,
                                        serverFingerprint, onClose }) {
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

        <Background settings={settings} update={update} palette={palette}
                    customWallpaper={customWallpaper} setCustomWallpaper={setCustomWallpaper} />

        <section>
          <h3>Text &amp; layout</h3>
          <div className="setting-row">
            <span>Text size</span>
            <Segmented label="Text size" value={settings.fontSize}
                       options={[['small', 'S'], ['medium', 'M'], ['large', 'L']]}
                       onChange={(fontSize) => update({ fontSize })} />
          </div>
          <label className="setting-row">
            <span>Compact messages</span>
            <input type="checkbox" className="toggle" checked={settings.compact}
                   onChange={(e) => update({ compact: e.target.checked })} />
          </label>
        </section>

        <Notifications settings={settings} update={update} />
        <Connection fingerprint={serverFingerprint} />
      </aside>
    </div>
  )
}
