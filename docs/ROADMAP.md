# Roadmap

Where the project stands and what could come next. **Help is welcome:** if
you would like to work on one of the open items, open an issue first (or
comment on an existing one) so we can agree on the approach, then send a
pull request to `develop`. See [CONTRIBUTING.md](../CONTRIBUTING.md).

## Done

- **v2:** single-thread epoll server, binary protocol, libsodium encryption
  (client ↔ server), own WebSocket implementation, terminal / web / desktop
  (Dear ImGui) clients.
- **v3:** performance work, heartbeat, message history (last 50, in memory),
  typing indicator, end-to-end encrypted private messages (DMs).
- **v4 (appearance):** messaging-app look on web and desktop, six themes,
  accent and bubble colors, wallpapers and custom images (dim, blur), text
  size, compact mode, emoji picker, unread count and notifications. The
  desktop app matches the web client's fonts, sizes and layout.

## Next

- [ ] Refresh the screenshots in `docs/images/` (several themes, a wallpaper,
      the settings panel, a DM) and describe themes and settings in the READMEs.

## Ideas, in suggested order

### Core

1. [ ] **Persistent history:** store messages on disk (SQLite or an
       append-only log) so they survive a server restart; load older messages
       when the user scrolls up. Stable message IDs are the base for replies,
       reactions, edits and search.
2. [ ] **Rooms / channels:** `#general`, `#games`, …; a room list in the
       sidebar, unread count per room. Needs protocol and server changes.
3. [ ] **Name ownership:** bind a name to the user's device key (already used
       for DMs) so nobody else can join under that name.

### Messaging

4. [ ] Replies (quote a message) and reactions (👍 ❤️ 😂).
5. [ ] Edit and delete messages.
6. [ ] File and image sharing: encrypted, sent in chunks, image previews.
7. [ ] Light formatting: `**bold**`, `` `code` ``, clickable links.
8. [ ] Delivered / read marks for DMs.
9. [ ] Search in messages.

### Security

10. [ ] **End-to-end encrypted rooms** (group keys, e.g. sender keys) so the
        server cannot read room messages either. Today only DMs are
        end-to-end encrypted. The largest and most careful piece of work:
        please discuss the design in an issue before writing code.
11. [ ] Moderation (kick, mute) and flood protection.

### Distribution

12. [ ] Packages: AppImage or Flatpak for the desktop app, a Docker image for
        the server, release builds attached to GitHub Releases.
13. [ ] Installable web app (PWA) with notifications on phones.
14. [ ] Windows and macOS builds of the desktop client in CI (the server
        stays Linux-only because it uses epoll).

## Known limits

- Combined emoji (skin tones, ZWJ sequences such as 👋🏽) show as separate
  glyphs in the desktop app; the picker only offers single emoji.
- The desktop app's color picker is ImGui's own, not the system dialog.
