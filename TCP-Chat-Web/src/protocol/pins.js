// Trust on first use for the browser, like known_servers in the terminal
// client. The first fingerprint seen for a host is stored; later ones must
// match. `storage` is localStorage in the browser (injectable for tests).

const KEY = 'tcpchat.known_servers'

function load(storage) {
  try {
    const pins = JSON.parse(storage.getItem(KEY) ?? '{}')
    return pins && typeof pins === 'object' ? pins : {}
  } catch {
    return {}
  }
}

function save(storage, pins) {
  storage.setItem(KEY, JSON.stringify(pins))
}

export const Pin = Object.freeze({
  NEW: 'new',           // first contact, now pinned
  MATCH: 'match',       // same key as before
  MISMATCH: 'mismatch', // different key: refuse
})

/** Checks (and on first use stores) the fingerprint for `host`. */
export function checkPin(storage, host, fp) {
  const pins = load(storage)
  const known = pins[host]
  if (!known) {
    pins[host] = fp
    save(storage, pins)
    return Pin.NEW
  }
  return known === fp ? Pin.MATCH : Pin.MISMATCH
}

export function pinnedFingerprint(storage, host) {
  return load(storage)[host] ?? null
}

/** Forgets a host so the next connection pins its new key. */
export function forgetPin(storage, host) {
  const pins = load(storage)
  delete pins[host]
  save(storage, pins)
}
