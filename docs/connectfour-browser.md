# Connect Four browser adapter

This adds a standalone, game-agnostic-seat-backed **Connect Four browser round**. Existing solo and ESP-NOW activities are unchanged.

The firmware retains authority over the seven-column/six-row board. It accepts strict 128-byte-maximum JSON command tuples for `join`, `drop`, `rematch`, and `resume`; only the currently claimed browser seat may drop a disc, and only on its turn. Mutations require the exact current revision. Authenticated resume deliberately allows a stale revision. First mover is the X4 (Light=1); browser plays Dark=2.

A bounded public snapshot contains phase, revision, turn, outcome, last move and a 42-digit board in column-major/bottom-first order. It never includes session secrets. The adapter reuses `webplay::SeatSession` for first-claim, authorization, disconnect token rotation and command throttling. Native host tests cover win, stale move rejection, unauthorized peers, resume, and rematch.

**The X4 Pro now serves the HTML game from its WPA-protected hotspot at `http://192.168.4.1/connectfour`.** Its CONNECT FOUR menu includes **PLAY BROWSER**. QR codes show WLAN credentials and the URL; HTTP port 80 serves the page and a public bootstrap token, while WebSocket port 81 receives strictly verified commands. The Developer Mode server is paused while this activity owns Wi-Fi, then resumed on exit. Those integrations, simulator UI and real hardware validation remain to be implemented. This adapter is an intentionally isolated first step, not a claim of playable browser Connect Four.

## Hardware acceptance remaining

Real X4 firmware loading and a full Safari/Chrome smartphone match have **not** been verified. Confirm both QR scans, password, WebSocket connection, six-disc stacks, winners, rematches, reconnect and proper radio/devmode cleanup on the physical device before changing the Draft PR to Ready.
