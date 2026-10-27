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

CAUTION: neither CardPartyCore nor the sample test fixture has an HTTP
server, QR UI, e-ink Activity, on-device SD pack reader or menu entry yet.
Those are integration tasks still to be implemented before First Playable is
complete. The rules header must not be presented as a finished game.

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
- Native X4 panel is 800x480 LANDSCAPE. Do not assume portrait support
  without implementing display and touch rotation.
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

- 800x480 landscape, full-width large legible question.
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
- Simulator screenshots for 800x480 X4 and real hardware validation.

## Follow-on integration sequence

1. Strict bounded protocol, multi-client authenticated session manager
   (seat ownership, reconnect token, no premature authorship leak), timeout.
2. X4 Activity, menu registration, host display, QR and hotspot lifecycle.
3. Browser client, touch-friendly scrolling card selection, judge/reveal UI.
4. Stable test-card deck and future indexed /games/cardparty/*.dat adapter.
5. Playwright multi-browser scenarios, end-to-end simulator and hardware soak.

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
