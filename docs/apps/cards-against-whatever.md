# Cards Against Whatever — First Playable

Status: Milestone 1 IN PROGRESS, isolated feature branch only. Do not advertise
a working device build or publish OTA until firmware/UI/network integration,
automated CI and real hardware have passed.

## Product architecture

The X4 Pro will be the *authoritative* party-game server and communal e-ink
host. Up to eight phones will act as browser clients, with private hands and a
rotating human Card Czar. The X4 will serve an HTTP page and WebSockets over
existing Wi-Fi or its own hotspot; a QR code will open the lobby. No account
and no internet connection will be needed during play.

Rando Cardrissian is a virtual *optional* anonymous player. The first mode
draws truly random legal answer cards and receives no human points. Learning
preferences and optional local/cloud LLM judging belong to later milestones.

## Implemented in the first change

- Header-only, freestanding C++17 rules engine at CardPartyCore.h.
- Lobby, 3–8 seats, connected/disconnected state retained by seat.
- Variable 5–10 card hands, prompt-driven ordered Pick 1/2/3 and Draw 2.
- Human Czar rotation, scoring, end-of-round reveal and match limits.
- Optional Rando card entry, hidden among real submissions until reveal.
- Public and seat-scoped private DTOs for a future secure transport.
- Host controls to close stalled submissions or abort a round.
- Reproducible tests in host-tests/cardparty, auto-discovered by existing CI.

Run: bash host-tests/cardparty/run.sh

The initial rules-only implementation has since been connected to an X4
server and a device Activity. Milestone 1 is still **unverified on real
hardware** and must not be released to OTA. The SD .dat pack reader is a
later milestone; the pilot uses original demo content.

## Visual design contract: N-O-D-E-inspired terminal / BBS (2026-10-27)

User reference: https://www.youtube.com/@NODEtv/videos ; https://n-o-d-e.net/
The user explicitly requested N-O-D-E aesthetics. This **supersedes**
the earlier generic black/white card wireframe direction.

This is a visual interpretation, NOT an instruction to reuse the NODE logo,
wordmark, artwork, exact screen layouts, or other copyrighted branding.

### Visual principles

- Mobile browser: full black (#000000) backdrop with white (#ffffff)
  typography; high contrast, no other accent colors.
- X4 Pro e-ink: only black/white pixels. Use white paper + black type where
  it significantly improves long-term viewing and ghosting; shared design
  language does not require identical inverted screen polarity.
- The X4 Pro is used upright: the renderer's default Portrait orientation
  (480x800, display and touch rotated) is what every shelf app uses.
- Monospace: system mono / Menlo / Consolas. No CDN fonts.
- Text-first editorial/terminal layout: abbreviated all-caps metadata,
  numbered options, thin horizontal rules, tight but intentional spacing.
- No rounded cards, box shadows, gradients, faux CRT scanlines, glow,
  colorful indicators, emoji decorations, busy illustrations, animations,
  or giant decorative ASCII art.
- Prompts are typographic focal points, with comfortable multiline
  wrapping. Do not force card text to ALL CAPS or shrink it unreadably.
- Answer choices are numbered vertical list rows: [01], [02], [03].
  Selection is indicated by reverse video (WHITE bg/BLACK text) and
  numbered selection order for Pick-2/Pick-3.
- Clearly visible, accessible connection state: CONNECTED / WAITING,
  ROUND 02/10, CZAR: MAX, 3 OF 5 SUBMITTED.
- Rando Cardrissian appears via text and is indistinguishable from a
  human submission prior to REVEAL.

### Player browser layout and interaction

- Touch-first, minimum 44px hit targets; card text >=16px.
- A compact fixed masthead with C/A/W, round, username, current czar.
- Long black prompts scroll within their own bounded area.
- Answer choices scroll independently, with visible scroll progress.
- A fixed/sticky bottom bar ALWAYS exposes count and [ SUBMIT ],
  regardless of answer-list scroll position. Safe-area aware.
- Answer submission requires exactly the specified number of DISTINCT
  card slots; Pick-2/Pick-3 preserve order.
- Separate clear phases: JOIN, LOBBY, SELECT, WAIT, JUDGE, REVEAL,
  FINAL. Anonymous judge view must never show authors until reveal.
- Judge chooses a numbered submission, sees its complete combined
  phrase and explicitly confirms.
- No unnecessary screen animation; respect reduced-motion preference.
- Keyboard focus remains highly visible. Inputs have text labels.

### E-ink game-master UI

- 480x800 portrait like every other shelf app: the X4 Pro is held upright,
  and a landscape-only screen made the host turn the device mid-shelf.
  Question wraps by measured width, up to 9 rows.
- Header: C/A/W / ROUND 02 / CZAR MAX.
- Footer: 3/5 ANSWERS SUBMITTED, optionally a compact progress indicator.
- QR lobby: large QR with quiet zone, readable local URL / code, roster.
- Prefer updates on actual phase, count, roster and result changes, not
  continuous animations or per-frame redraws.
- Use familiar shelf/navigation behavior and existing Toybox font support.

### Sketch (PHONE)

    C/A/W                         02/10
    PLAYER IVAN               CZAR MAX
    ------------------------------------
    PROMPT
    Why did production go down?
    ____
    ------------------------------------
    YOUR HAND                     1/2
    [01] A technician with root access
    [02] A coffee maker with IPv6
    [03] A broken DNS server
    [04] ...
    ------------------------------------
    1/2 SELECTED          [ SUBMIT ]

### Acceptance criteria for browser UI

- 320px, 390px and desktop viewports.
- Very long German prompts, long answers, umlauts and Pick-3.
- Visible list position and independently operable scroll areas.
- No clipping behind sticky footer or system virtual keyboard.
- Precise anonymous response secrecy, reconnect and offline assets.
- Simulator screenshots for the 480x800 X4 Pro and real hardware validation.

## Shared design system

This app is the first pilot of the proposed
[CrossPlay Terminal design system](../node-terminal-design-system.md).
Keep the user-visible NODE-inspired 1-bit, text-first design consistent across
the browser client and the X4 host. Do not treat the pilot as permission to
re-theme existing unrelated apps or change the EPUB reader.

## Session-authentication foundation (second isolated change)

- `CardPartySessions.h`: allocation-free 8-seat gateway, separately
  authenticated connection IDs and 128-bit secret tickets.
- Supports disconnect/seat retention and reconnect with **token rotation**,
  rejects duplicate sockets and secrets, checks revision before each action.
- Exposes only public game snapshots or **ticket-authorized** private hand DTOs.
- Host-only start/next/close/abort methods are separate from browser command
  authorization; none of these should be routed to an unauthenticated client.
- Secrets must come from ESP hardware entropy (e.g. esp_fill_random); the
  deterministic game shuffle RNG is NOT suitable for generating session tokens.
- The gateway does not encrypt Wi-Fi/WebSocket traffic. Secure local hotspot
  configuration remains a separate integration requirement.
- `host-tests/cardparty/test_sessions.cpp` exercises wrong-token, replay,
  stale-close, reconnect and token rotation cases.

**Implemented since this section was written:** bounded HTTP/WebSocket
adapter, connected-socket ownership, strict JSON command parser, NODE-style
browser client, QR/e-ink X4 activity and shelf registration. **Not yet
accepted:** real-device/mobile end-to-end validation, formal cross-origin
WebSocket handshake policy, SD deck reader, release gates.

## NODE-style browser client scaffold (third isolated change)

- `web/CardPartyPage.html`: standalone high-contrast text-first browser
  client, matching the [shared design system](../node-terminal-design-system.md).
- Contains lobby, private hand, ordered Pick 1/2/3 selection with independent
  scrolling, sticky SUBMIT, judge/confirm, reveal and scores.
- No remote CDN/font, black/white only, touches 48px high, dynamic text assigned
  through textContent rather than unsafe HTML injection.
- Reconnect asks the X4 to resume using the session-scoped ticket; the expected
  future server adapter must validate all messages and rotate the proof.
- `host-tests/cardparty/test_browser_page.cjs`: Node-only mocked DOM/WebSocket
  flow for join, Pick 2, submit, anonymous judge, reveal and reconnect.

**Updated:** The page is embedded by `scripts/build_html.py` and served by
`CardPartyBrowserServer.cpp` on HTTP 80; cards, submissions and reconnect
use the dedicated WebSocket on port 81. Server and client use the same strict
bounded wire-command format. Host tests exercise the protocol and simulated
Wi-Fi/WebSocket server, but these do **not** prove real smartphone or X4
hardware correctness.

## Network security and socket capacity hardening (2026-10-27)

- The X4 Pro firmware build now sets `WEBSOCKETS_SERVER_CLIENT_MAX=10`
  in `[x4pro_common]` (used by device/release environments). The
  arduinoWebSockets library otherwise defaults to **5 simultaneous client
  sockets**, insufficient for eight real human players. Two extra sockets
  provide reconnect/handshake headroom; the local Wi-Fi hotspot also
  permits up to 10 associated stations while the game stays capped at eight
  authenticated player seats. ESP32 heap and real station capacity still
  require hardware soak testing.
- WebSocket upgrade requests must include a browser `Origin` matching
  `http://<device-IP>` (port `:80` is also accepted). The embedded
  arduinoWebSockets `onValidateHttpHeader` hook rejects absent or foreign
  Origins **during the HTTP handshake**, before assigning a seat/token.
  The QR code uses the device-IP URL; alternative hostnames are not currently
  supported for cross-origin handshakes.
- A native client can spoof an Origin header, so this is **cross-site
  WebSocket hijacking mitigation**, not transport encryption or network
  access control. All client actions still require a seat-bound 128-bit token.
  The shared-LAN mode uses `ws://` without TLS; a protected local hotspot
  is preferable. Treat untrusted LANs as out-of-scope until a secure
  transport is designed and tested.
- Native server tests exercise accepted localhost-IP Origins and rejected
  missing/foreign Origins; the host test runner also checks the physical
  X4 Pro `platformio.ini` socket-capacity build flag so a stub cannot hide
  an accidental return to the library default.
- Changes are in **Draft PR #47 only**. Prior Nightly
  `v1.16.13-rc.20261027.ge3bdea3c` is immutable and does *not* contain
  these hardening changes. A new OTA must be independently approved.

## Session lifecycle and slot hardening (2026-10-27, second pass)

Found by code review of the reconnect path and reproduced in
`host-tests/cardparty/test_stress.cpp` before fixing:

| Defect | Effect on a real 8-phone table | Fix |
|---|---|---|
| Lobby seat of a vanished browser (QR opened in-app, then in Safari) stays seated forever | 8 seats used by 7 people; the eighth cannot join | A full lobby reclaims a *disconnected* lobby seat; live seats are never displaced; the reclaimed ticket answers `expired` |
| Socket that never joins (join screen left open, refused ninth player) holds a library slot and reconnects every 1.5 s | Three idle tabs exhaust the 10 slots; seated players cannot reconnect | X4 closes sockets that are not bound to a seat after `kAuthGraceMs` (20 s); the browser reconnects only while it holds a ticket, otherwise on JOIN |
| Last pending answerer locks the phone → immediate `DISCONNECTED` → round jumps to Judging | The slowest player is skipped by a 2 s app switch | Pending answerer's turn is *held*. After `kBlockAfterMs` (15 s) offline the table blocks and every screen names the missing player until they are back or the X4 goes on without them (`CardPartyTable.h`); the host can still close submissions at any time |
| X4 BACK ended a running session immediately | One stray press kills the party for everyone | Second BACK within 7 s required once players are seated |
| New match = hotspot restart with a new random password | Every phone must rejoin Wi-Fi and rescan the QR | `[ REMATCH ]` keeps network, seats, names and tickets; scores and hands reset; phones land in the lobby |
| Rejected submit/judge (duplicate, stale revision) only returned `error` | Button stays disabled until the next broadcast | Authenticated seat gets an immediate state resync after the error |
| Browser limited names by UTF-16 length, server by 23 bytes | `Jürgen-Schäfer-Müller` failed with a generic error | Browser checks UTF-8 bytes; server explains `SESSION FULL`, `GAME ALREADY RUNNING`, `ALREADY JOINED` |
| No telemetry while CardParty pauses DevMode | Heap/watchdog acceptance not measurable | `Server::diagnostics()`: heap, min heap, largest block, PSRAM, sockets, AP stations, seats, rejects, takeovers, idle closes, send failures, reset reason. Serial `CARDPARTY diag …` every 15 s; one `SYS H…K M…K WS.. AP..` line on the X4 footer, repainted only with normal game refreshes |

Diagnostics never include tickets, names or cards.

### TCP back-pressure (half-dead phone)

arduinoWebSockets writes block while a peer's TCP window is closed: the
ESP32 network client retries `select()` for up to ~10 s per write, the
library's own heartbeat pings through the same path, and `disconnect()`
writes a close frame first. One phone in airplane mode with the page open
froze the X4 loop for everyone, repeatedly, until the heartbeat gave up.

`SocketServer` (a thin `WebSocketsServer` subclass) adds two calls that
never block: `writable()` (zero-timeout `select()` on the client socket;
lwIP reports writable while more than `TCP_SNDLOWAT` bytes are free:
2,873 of the 5,744-byte send buffer on this build, above the largest
measured state frame of ~2.2 KB) and
`drop()` (library `clientDisconnect()` without a close frame). The server
uses them everywhere it writes:

- State frames to an unwritable socket are deferred; the seat keeps one
  pending flag (welcome outranks state) and gets the current snapshot once
  the buffer drains. Errors and `expired` to such a socket are skipped.
- The library heartbeat is off. `heartbeat()` pings writable sockets every
  5 s; any frame from the browser (pong, command) proves liveness. Two
  unanswered pings, or 15 s without a writable buffer, drop the socket.
- Takeover and idle-socket closes send a close frame only to a writable
  socket and drop the rest.

`test_backpressure.cpp` models a stalled slot (every write to it counts as a
blocking write) and a silent one (no pongs). It checks: no blocking write
in any path, the other seven phones get every update, the dead phone is
dropped (never closed with a frame), a slow phone gets exactly one current
frame after draining, a deferred welcome still delivers its ticket, and
eight healthy phones survive two minutes of heartbeats. Against the
previous server it fails 19 checks.

Not covered: a writable socket whose free space is smaller than a large
frame still blocks until the phone ACKs; the HTTP page download (port 80)
is not gated.

### Stress test model

`test_stress.cpp` drives the real `Server`, `Sessions` and `Game` with eight
browser clients against a fake transport that models what matters on the X4
Pro: ten numbered slots (`WEBSOCKETS_SERVER_CLIENT_MAX=10`), lowest-free slot
reuse, refusal when full, and frames to closed slots being lost. Three seeds,
ten rounds each with the real demo deck, reloads (overlapping and clean) in
Lobby/Playing/Judging/Reveal, duplicate submits and judge replays, a rematch,
plus dedicated ghost-seat, held-turn and same-socket resume cases. After every
step it checks: one socket per seat, every seat bound to an open slot, no slot
leak, at most one human point per round, hands identical across reloads, and
that every ticket only ever travels to the socket it was issued to. Worst
measured state frame: ~2.2 KB of the 6 KB bound.

Mutation check: removing the takeover `ws_.disconnect` or the idle-socket
close each fails the suite (>130 failed checks).

**Not proven by this test:** radio behaviour, real lwIP send-buffer
behaviour (modelled in `test_backpressure.cpp`), heap under real TLS-less
traffic, e-ink refresh. See
[cards-against-whatever-hardware-acceptance.md](cards-against-whatever-hardware-acceptance.md).

## Milestone 1 implementation and remaining acceptance gates

### Implemented on the feature branch

1. `CardPartyCore.h` + host tests: 3–8 players, Pick 1/2/3, Draw 2,
   rotating Czar, Rando Cardrissian, scoring and private/public DTOs.
2. `CardPartySessions.h`: per-connection 128-bit ticket, revision and resume
   ticket rotation. Sender must be an active WebSocket owner.
3. `web/CardPartyProtocol.h`: strict bounded JSON parser/escaped writer;
   only join, resume, submit and judge are allowed from phones.
4. `web/CardPartyBrowserServer.{h,cpp}`: HTTP 80 serves locally embedded
   `/cardparty`; port 81 serves WebSockets, 8-seat authorization and
   per-connection hands. X4 physically controls start/next/close/abort.
5. `CardPartyActivity.{h,cpp}`: X4 480×800 portrait e-ink host view and QR lobby,
   WPA2 hotspot (unique generated password), existing Wi-Fi via the system network picker,
   progress, phase and winner; terminal-style UI, Games shelf registration.
6. `CardPartyDemoDeck.h`: original German sample cards; no third-party
   card-content licensing assumptions.
7. `web/CardPartyPage.html`: NODE-style monochrome mobile UI with
   scrolling prompt and hands, ordered selection with preserved scroll
   position, sticky SUBMIT, anonymous judging and reconnect handling.
8. CI host suites: game simulation, session authentication, JSON parser,
   HTTP/WebSocket native fake-network tests (including eight players),
   DOM mock and browser command-sequence verification.

### Before marking First Playable complete

- [ ] Entire latest branch CI green (firmware, simulator, host suites,
      site documentation consistency; do not trust earlier superseded runs).
- [ ] Real X4 local hotspot: WPA2 credentials, QR phone join, long-session
      E-ink refresh, memory/free heap and watchdog observation.
- [ ] Three or more real phones: separate hands, ordered Pick 3, random
      Rando entry, hidden authors, real czar scoring and ten complete rounds.
- [ ] Eight real/browser clients soak including disconnects, socket exhaustion,
      reconnect-token rollover, shared-local-network risks and stall recovery.
      Procedure and pass criteria:
      [hardware acceptance plan](cards-against-whatever-hardware-acceptance.md).
- [x] Browser viewport snapshots 320 px, 390 px and desktop with long
      German text: `test_browser_viewport.cjs` in CI (Chromium). It found
      answer cards squeezed below their text height and overlapping at
      320 px, and an unwrapped judging prompt that scrolled the page
      sideways; both fixed.
- [ ] Same on real iOS Safari and Android Chrome (toolbars, safe area,
      keyboard, font scaling); RTL not required unless localization supports it.
- [ ] Resolve any device-specific payload limits or refresh defects.
- [ ] OTA disabled until acceptance and explicit release decision.

### Follow-on deck milestone

Replace static test data with versioned indexed SD `/games/cardparty/*.dat`
packs, atomic download/install and localization-compatible metadata.
AI preference learning, Rando ranking and remote LLM judging are distinct
later phases, not part of this First Playable gate.

## Safety invariants

- Never serialize Game internals to browsers. publicView shows no authors
  during Judging and no submitted card IDs during Playing; privateHand is
  authorized by the transport separately.
- Never trust a browser-supplied player index, card ID, phase or score.
- Authenticated submission: strictly bounded valid ordered slot indexes,
  a single submission per round and only the active connected seat.
- Only the selected Card Czar can judge; invalid actions do not score.
- Interrupted downloads must never overwrite installed SD content.
- The rules PR must not change main firmware behavior and must not publish OTA.

## References

- Hotspot Arcade Fill the Blank: ESP32 party-game reference
  https://github.com/tarikbc/hotspot-arcade
- Cards Over LAN JSON deck schema
  https://github.com/BTFighter/cards-over-lan
- Current repo multiplayer transport
  docs/battleship-browser.md
- Current repo SD pack model
  docs/apps/trivia-pack-format.md
