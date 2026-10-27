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

## Visual design contract: monochrome wireframe (user requirement, 2026-10-27)

Applies to BOTH the X4 e-ink Game Master UI and the phone/browser client.
Treat this as an acceptance criterion, not an optional design inspiration.

- **Strict black & white:** Use only #000000 and #ffffff as visual colors.
  Do not inherit Battleship's cream/dark theme, gradients, tinted surfaces,
  grays, colorful player markers or CSS color scheme preference.
  Favor white canvas and black text; reverse black/white for the current
  prompt card, a selected answer, and a primary action.
- **Wireframe styling:** Flat 1–2px rectangular borders, sharp corners,
  simple dividers, generous empty space, no shadows, glow, background textures,
  illustrations, decorative icons, emoji decorations or transitions.
  Selection must be recognizable without animation, hover, or color.
- **Typography:** System sans/mono stack already available on the device;
  no downloaded fonts. Short uppercase metadata headings. Reading text uses
  normal sentence case; minimum 16px on phones, 18px on e-ink where the
  available canvas allows it. Never shrink text until unreadable or truncate
  a prompt/answer silently. Keep natural word wrapping.
- **Cards:** Black prompts are black rectangles with white text, white
  answers are bordered white rectangles with black text. A chosen answer
  reverses to black/white and gains an explicit "SELECTED" label and visible
  ordering for Pick 2/3 (1, 2, 3). Unselected answers are never hidden.
- **Phone ergonomics:** Mobile-first at 320px+ viewport, touch targets at
  least 44x44 CSS px, consistent spacing, scrollable prompt area AND
  independently scrollable answer list. A visible scrollbar/position cue is
  preferred; no horizontal scrolling. Sticky bottom action bar with current
  selection count and SUBMIT, reachable regardless of list scroll position.
  Disabled SUBMIT is explicit until exactly pick N distinct cards are selected.
- **Card Czar:** Dedicated judgment state with shuffled anonymous responses.
  Tap one numbered submission, review the whole assembled text, then confirm
  the winner; do not reveal authors or hidden card ownership prematurely.
- **Lobby:** Game code/QR, connected players, readiness and Start controls
  must be legible from across a table. On the e-ink host, render only the
  current prompt, round, submit count, Czar identity and eventual scores.
- **E-ink refresh discipline:** Avoid animation and unnecessary redraws.
  Update when joining/leaving, round changes, submit count changes,
  judgment begins or a result is chosen; use the existing renderer's
  partial/full refresh policies. QR requires sufficient quiet margin.
- **Responsive/browser behavior:** High-contrast focus outline, keyboard
  navigation, visible connection/reconnect state, safe-area padding, no
  third-party CDN/JS/font dependencies; work with light/dark OS themes by
  forcing the fixed monochrome palette.
- **Localization:** German text and umlauts must fit without destructive
  clipping. All controls and player-visible labels must be localizable.
  Use clear words, not icon-only ambiguous actions.

### Reference wireframes (structure, not pixel-art assets)

Phone player:

    +--------------------------------+
    | CARDS AGAINST WHATEVER  R 2/10 |
    | YOU: IVAN    CZAR: MAX         |
    +--------------------------------+
    | [ BLACK PROMPT / SCROLL ]      |
    | Why is the server down? ____   |
    +--------------------------------+
    | YOUR HAND             1/2      |
    | [1] Answer A                   |
    | [ ] Answer B                   |
    | [2] Answer C                   |
    |  ...  (SCROLL)                 |
    +--------------------------------+
    | 2 OF 2 CHOSEN   [ SUBMIT ]     |
    +--------------------------------+

X4 Game Master:

    +--------------------------------+
    | CAW  / ROUND 02   CZAR: MAX    |
    +--------------------------------+
    | [      BLACK PROMPT          ] |
    | [   LARGE WRAPPED TEXT       ] |
    +--------------------------------+
    | ANSWERS 3/5  [------    ]      |
    | CONNECTION: LOCAL             |
    +--------------------------------+

Before implementing HTML or e-ink screens, make layout snapshots for
320px/390px/desktop browser and 480x800 portrait e-ink, checking long DE
prompts, Pick 3, virtual keyboard safe area, large text, focus and scroll.
A visually appealing screenshot is not a substitute for touch/overflow tests.


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
