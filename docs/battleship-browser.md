# Battleship browser crossplay

The X4 Pro owns the browser game in `BattleshipLocalPlayActivity`. The existing
X4 Pro / ESP-NOW path returns to `LinkActivity` unchanged. Browser is always
side 1 and places first. The browser path now supports a complete match:
profile, browser fleet placement, X4 fleet placement, alternating shots, game
over and a same-session rematch.

## Transport boundary

HTTP serves `/battleship` on port 80; WebSocket uses port 81. The page obtains a
128-bit random session capability through a same-origin, uncached GET to
`/battleship/session`. That endpoint has no CORS allowance. The page is not
frameable. This is local-session/CSRF protection, not account authentication
or encrypted transport; the existing hotspot remains open.

The first accepted profile binds the browser seat to that WebSocket client.
Other clients receive only the public status DTO and cannot issue commands.
A heartbeat detects disconnects. Losing the owner rotates the capability and
clears an unconfirmed profile/draft. Reconnection requires a fresh capability
and profile. Once a fleet is confirmed it stays locked for that round. Leaving
the browser session on the X4 starts a new game. There is no spectator or seat
takeover mode.

Only these bounded JSON tuples are accepted (token below abbreviated):

```text
["profile","<32 lowercase hex digits>",revision,hair,eyes,mouth]
["place","<32 lowercase hex digits>",revision,shipIndex,bow,horizontal]
["ready","<32 lowercase hex digits>",revision]
["fire","<32 lowercase hex digits>",revision,cell]
["rematch","<32 lowercase hex digits>",revision]
```

- Profile: exactly three integers, each 0..13. Name and avatar are derived on
  the X4 through `player::compose` and `player::drawAvatar`, never supplied as
  text or artwork by the browser. Profile is session-only, not saved to SD.
- Place: ship index 0..4, bow 0..99, horizontal 0 or 1. The X4 applies the
  existing core's board and collision checks against the browser's draft.
- Ready: all five ships must form a valid fleet. `bship::place(game, 1, …)`
  commits the browser fleet and sends the X4 into its own placement screen.
  When the X4 confirms, both sides enter the normal playing phase.
- Fire: cell 0..99. It is accepted only while the browser owns the turn and the
  target has not already been fired on. `BattleshipCore` remains authoritative
  for hit/miss, sunk state, turn changes and game over.
- Rematch: accepted only after game over. It resets both fleets and all shot
  state while keeping the same browser owner, capability and player profile.
  The browser immediately starts a fresh placement round, followed by X4
  placement again.
- Revision starts at zero and advances once per accepted action, including
  shots and rematches. Stale/duplicate revisions and wrong-phase commands are
  refused.
- Maximum application command is 128 bytes. Parsing and DTO serialization have
  no per-command heap allocation. Commands are limited to one per 100 ms.
  Unknown commands, extra/missing values, negative/fractional/coerced integers
  and overflow are refused. Binary and fragmented messages never enter the
  command dispatcher.

`Server` does not include or accept `bship::Game`. A function-pointer callback
passes a typed command to the activity, which owns `BrowserPlayer` and the game.
The activity protects game changes against rendering with `RenderLock`. The
private command response buffer is a fixed 256-byte server member.

## Responses and secrecy

The public snapshot exposes only the information the browser player is allowed
to know: phase/turn/winner, the browser's shot results against the X4, and the
shots received by the browser fleet. Unknown X4 fleet cells remain unknown.
The compact wire form stays within the server's fixed snapshot buffer.

Only the owner receives `placement` replies with `accepted`, `revision`,
`profile`, `ready`, three validated `slots`, and five `[bow,horizontal]` pairs
from **its own fleet**. Bow 255 means that ship has not been placed. The same
private reply carries the new revision after a shot or rematch without exposing
host fleet state.

The browser draws only acknowledged placement and public battle information.
No serializer accepts a raw `bship::Game`; the X4 fleet itself is never sent to
the browser. A hit identifies only a cell after the browser has fired there.

Back closes WebSocket, HTTP, DNS and mDNS before the existing network and
nested Developer Mode ownership is released. HTTP/captive-portal probes do
not count as a connected player.

## Verification

`host-tests/battleship/run.sh` covers strict parsing, all 2,744 profiles,
placement bounds/overlap, duplicate revisions, premature/repeated confirmation,
turn-checked firing, duplicate targets, rematch phase/revision behavior,
projection secrecy, capability rotation, private replies, throttling, radio
yields, cleanup and browser controls. `host-tests/link/run.sh` covers the
unchanged ESP-NOW logic. The simulator builds the UI path while retaining its
disabled hardware-network backend.

On hardware:

1. Games → Battleship → PLAY NEARBY → BROWSER → HOTSPOT. Join
   `CrossPlay-Battleship` and open the QR URL or displayed IP URL.
2. Choose the browser profile and place all five browser ships. Confirm the
   fleet and verify that the X4 automatically switches to its own fleet setup.
3. Confirm the X4 fleet. The browser should show **YOUR MOVE** first. Fire from
   the browser, then fire from the X4. Check turn changes, hits, misses and sunk
   ships on both displays until one fleet is destroyed.
4. At game over, press **PLAY AGAIN** in the browser. The same player profile
   must remain selected, both boards and all shots must clear, and browser fleet
   placement must start again without reconnecting or restarting the hotspot.
5. Repeat through BROWSER → WI-FI. With Developer Mode enabled, repeatedly enter
   and leave both modes; its normal service should return after Back. Then
   select X4 PRO and exercise a normal ESP-NOW match on two devices.
