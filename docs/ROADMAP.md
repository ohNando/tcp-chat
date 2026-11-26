# Roadmap

Where the project stands and what could come next. **Help is welcome:** every
item below has an issue with the options we considered. Comment on it before
you start, so we can agree on the approach, then send a pull request to
`develop`. Items labeled
[good first issue](https://github.com/ohNando/tcp-chat/labels/good%20first%20issue)
are a gentle start. See [CONTRIBUTING.md](../CONTRIBUTING.md).

## Done

- **v2:** single-thread epoll server, binary protocol, libsodium encryption
  (client ↔ server), own WebSocket implementation, terminal / web / desktop
  (Dear ImGui) clients.
- **v3:** performance work, heartbeat, message history (last 50, in memory),
  typing indicator, end-to-end encrypted private messages (DMs).
- **v4 (appearance):** messaging-app look on web and desktop, six themes,
  accent and bubble colors, wallpapers and custom images (dim, blur), text
  size, compact mode, emoji picker, unread count and notifications. The
  desktop app matches the web client's fonts, sizes and layout. Screenshots
  and READMEs are up to date.
- **Persistent history** ([#1](https://github.com/ohNando/tcp-chat/issues/1)):
  room messages in SQLite with stable ids; older messages load when
  scrolling up.

## Ideas, in suggested order

### Core

2. [ ] [#2](https://github.com/ohNando/tcp-chat/issues/2) **Rooms / channels:** `#general`, `#games`, …
       - A WhatsApp/Discord-style room panel on the left: last message and
         unread count per room.
       - Optional password-protected rooms. A password limits who can join;
         it does not hide the room from the server (that is item 10).
       - Needs protocol and server changes.
3. [ ] [#3](https://github.com/ohNando/tcp-chat/issues/3) **Name ownership:** bind a name to the user's device key (already used
       for DMs) so nobody else can join under that name.

### Messaging

4. [ ] [#4](https://github.com/ohNando/tcp-chat/issues/4) Replies (quote a message) and reactions (👍 ❤️ 😂).
5. [ ] [#5](https://github.com/ohNando/tcp-chat/issues/5) Edit and delete messages.
6. [ ] [#6](https://github.com/ohNando/tcp-chat/issues/6) File and image sharing: encrypted, sent in chunks, image previews.
7. [ ] [#7](https://github.com/ohNando/tcp-chat/issues/7) Light formatting: `**bold**`, `` `code` ``, clickable links.
8. [ ] [#8](https://github.com/ohNando/tcp-chat/issues/8) Delivered / read marks for DMs.
9. [ ] [#9](https://github.com/ohNando/tcp-chat/issues/9) Search in messages.

### Security

10. [ ] [#10](https://github.com/ohNando/tcp-chat/issues/10) **End-to-end encrypted rooms** (group keys, e.g. sender keys) so the
        server cannot read room messages either. Today only DMs are
        end-to-end encrypted. The largest and most careful piece of work:
        please discuss the design in an issue before writing code.
11. [ ] [#11](https://github.com/ohNando/tcp-chat/issues/11) Moderation (kick, mute), by room owners or the server operator.
        Flood protection (a rate limit per connection) is done.

### Distribution

12. [ ] [#12](https://github.com/ohNando/tcp-chat/issues/12) Packages: AppImage or Flatpak for the desktop app, a Docker image for
        the server, release builds attached to GitHub Releases.
13. [ ] [#13](https://github.com/ohNando/tcp-chat/issues/13) Installable web app (PWA) with notifications on phones.
14. [ ] [#14](https://github.com/ohNando/tcp-chat/issues/14) Windows and macOS builds of the desktop client in CI (the server
        stays Linux-only because it uses epoll).

## Known limits

- Combined emoji (skin tones, ZWJ sequences such as 👋🏽) show as separate
  glyphs in the desktop app; the picker only offers single emoji.
- The desktop app's color picker is ImGui's own, not the system dialog.
