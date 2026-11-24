import { test } from 'node:test'
import assert from 'node:assert/strict'

import { groupMessages, dayLabel, GROUP_GAP_MS } from './groupMessages.js'

const NOW = new Date(2026, 10, 24, 15, 0).getTime()
let id = 0
const chat = (name, ts, extra = {}) => ({ id: ++id, kind: 'chat', name, text: 'x', ts, ...extra })
const kinds = (items) => items.map((i) => i.type === 'bubble' ? `${i.m.name}:${i.first ? 'F' : ''}${i.last ? 'L' : ''}` : i.type)

test('consecutive messages from one person form a group', () => {
  const items = groupMessages([chat('ali', NOW - 3000), chat('ali', NOW - 2000), chat('ali', NOW - 1000)], NOW)
  assert.deepEqual(kinds(items), ['date', 'ali:F', 'ali:', 'ali:L'])
})

test('another sender, a system line or a long pause starts a new group', () => {
  const items = groupMessages([
    chat('ali', NOW - 20 * 60000),
    chat('ali', NOW - 20 * 60000 + GROUP_GAP_MS + 1),     // too late
    chat('veli', NOW - 10 * 60000),                        // other person
    { id: ++id, kind: 'system', text: 'x joined', at: NOW - 9 * 60000 },
    chat('veli', NOW - 8 * 60000),                         // after a system line
  ], NOW)
  assert.deepEqual(kinds(items), ['date', 'ali:FL', 'ali:FL', 'veli:FL', 'line', 'veli:FL'])
})

test('private messages group per conversation, not with room messages', () => {
  const items = groupMessages([
    chat('ali', NOW - 3000),
    { id: ++id, kind: 'dm', name: 'ali', to: 'veli', ts: NOW - 2000 },
    { id: ++id, kind: 'dm', name: 'ali', to: 'zeynep', ts: NOW - 1000 },
  ], NOW)
  assert.deepEqual(kinds(items), ['date', 'ali:FL', 'ali:FL', 'ali:FL'])
})

test('a new day inserts a separator and breaks the group', () => {
  const yesterday = NOW - 24 * 3600 * 1000
  const items = groupMessages([chat('ali', yesterday), chat('ali', NOW)], NOW)
  assert.deepEqual(items.filter((i) => i.type === 'date').map((i) => i.label), ['Yesterday', 'Today'])
  assert.deepEqual(kinds(items), ['date', 'ali:FL', 'date', 'ali:FL'])
})

test('older days get a date label', () => {
  assert.equal(dayLabel(NOW, NOW), 'Today')
  assert.notEqual(dayLabel(NOW - 5 * 24 * 3600 * 1000, NOW), 'Yesterday')
})
