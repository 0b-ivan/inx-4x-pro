# Connect Four browser adapter

This adds a standalone, game-agnostic-seat-backed **Connect Four browser round**. Existing solo and ESP-NOW activities are unchanged.

The firmware retains authority over the seven-column/six-row board. It accepts strict 128-byte-maximum JSON command tuples for `join`, `drop`, `rematch`, and `resume`; only the currently claimed browser seat may drop a disc, and only on its turn. Mutations require the exact current revision. Authenticated resume deliberately allows a stale revision. First mover is the X4 (Light=1); browser plays Dark=2.

A bounded public snapshot contains phase, revision, turn, outcome, last move and a 42-digit board in column-major/bottom-first order. It never includes session secrets. The adapter reuses `webplay::SeatSession` for first-claim, authorization, disconnect token rotation and command throttling. Native host tests cover win, stale move rejection, unauthorized peers, resume, and rematch.

**The standalone HTML browser page and Node interaction tests now exist, but the page is not yet served by an X4 HTTP/WebSocket route and is not wired into the X4 menu.** Those integrations, simulator UI and real hardware validation remain to be implemented. This adapter is an intentionally isolated first step, not a claim of playable browser Connect Four.