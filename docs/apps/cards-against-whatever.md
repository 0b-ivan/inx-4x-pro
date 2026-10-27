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
