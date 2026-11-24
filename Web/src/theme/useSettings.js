import { useCallback, useEffect, useMemo, useState } from 'react'

import { DEFAULT_DARK, DEFAULT_LIGHT, THEMES, resolveTheme, themeVars } from './themes.js'

const STORAGE_KEY = 'tcpchat.settings'

function systemTheme() {
  return window.matchMedia?.('(prefers-color-scheme: light)').matches ? DEFAULT_LIGHT : DEFAULT_DARK
}

export const DEFAULT_SETTINGS = Object.freeze({
  theme: null,          // null = follow the system (Midnight / Daylight)
  accent: null,         // null = the theme's own color
  ownBubble: null,
  otherBubble: null,
  wallpaper: 'none',    // 'none' | built-in id | 'custom'
  dim: 0.25,            // 0..0.8 darkening over the wallpaper
  blur: 0,              // px
  fontSize: 'medium',   // 'small' | 'medium' | 'large'
  compact: false,
  notifications: false,
})

function load() {
  try {
    const saved = JSON.parse(window.localStorage.getItem(STORAGE_KEY) ?? '{}')
    return { ...DEFAULT_SETTINGS, ...(saved && typeof saved === 'object' ? saved : {}) }
  } catch {
    return { ...DEFAULT_SETTINGS }
  }
}

/**
 * User preferences for the look of the app, persisted in localStorage and
 * applied as CSS custom properties on <html>.
 */
export function useSettings() {
  const [settings, setSettings] = useState(load)

  const update = useCallback((patch) => {
    setSettings((s) => {
      const next = { ...s, ...patch }
      window.localStorage.setItem(STORAGE_KEY, JSON.stringify(next))
      return next
    })
  }, [])

  const themeId = settings.theme && THEMES[settings.theme] ? settings.theme : systemTheme()
  const { accent, ownBubble, otherBubble } = settings
  const palette = useMemo(() => resolveTheme(themeId, { accent, ownBubble, otherBubble }),
                          [themeId, accent, ownBubble, otherBubble])

  useEffect(() => {
    const root = document.documentElement
    for (const [k, v] of Object.entries(themeVars(palette))) root.style.setProperty(k, v)
    root.style.colorScheme = palette.dark ? 'dark' : 'light'
    root.dataset.theme = themeId
    root.dataset.fontSize = settings.fontSize
    root.dataset.compact = settings.compact ? 'true' : 'false'
  }, [palette, themeId, settings.fontSize, settings.compact])

  return { settings, update, themeId, palette }
}
