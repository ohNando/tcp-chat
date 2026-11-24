// Trust on first use for the browser, like known_servers in the terminal
// client. The first fingerprint seen for a host is stored; later ones must
// match. `storage` is localStorage in the browser (injectable for tests).

const KEY = 'tcpchat.known_servers'
// Users' private-message keys, per "host/name" (see useChat.js).
export const PEER_PINS = 'tcpchat.known_peers'

function load(storage, key = KEY) {
  try {
    const pins = JSON.parse(storage.getItem(key) ?? '{}')
    return pins && typeof pins === 'object' ? pins : {}
  } catch {
    return {}
  }
}

function save(storage, pins, key = KEY) {
  storage.setItem(key, JSON.stringify(pins))
}

export const Pin = Object.freeze({
  NEW: 'new',           // first contact, now pinned
  MATCH: 'match',       // same key as before
  MISMATCH: 'mismatch', // different key: refuse
})

/** Checks (and on first use stores) the fingerprint for `host`. */
export function checkPin(storage, host, fp, key = KEY) {
  const pins = load(storage, key)
  const known = pins[host]
  if (!known) {
    pins[host] = fp
    save(storage, pins, key)
    return Pin.NEW
  }
  return known === fp ? Pin.MATCH : Pin.MISMATCH
}

export function pinnedFingerprint(storage, host, key = KEY) {
  return load(storage, key)[host] ?? null
}

/** Forgets a host so the next connection pins its new key. */
export function forgetPin(storage, host, key = KEY) {
  const pins = load(storage, key)
  delete pins[host]
  save(storage, pins, key)
}
