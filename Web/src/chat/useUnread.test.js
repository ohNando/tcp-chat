import { test } from 'node:test'
import assert from 'node:assert/strict'

import { isNotable } from './useUnread.js'

test('DMs and mentions are notable, own and replayed messages are not', () => {
  assert.ok(isNotable({ kind: 'dm', name: 'ali', text: 'x' }, 'Ayşe'))
  assert.ok(isNotable({ kind: 'chat', name: 'ali', text: 'selam AYŞE nasılsın' }, 'Ayşe'))
  assert.ok(!isNotable({ kind: 'chat', name: 'ali', text: 'herkese selam' }, 'Ayşe'))
  assert.ok(!isNotable({ kind: 'dm', name: 'Ayşe', text: 'x', own: true }, 'Ayşe'))
  assert.ok(!isNotable({ kind: 'chat', name: 'ali', text: 'Ayşe', history: true }, 'Ayşe'))
  assert.ok(!isNotable({ kind: 'system', text: 'Ayşe joined' }, 'Ayşe'))
})
