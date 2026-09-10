# Battleship browser profiles and placement

The X4 Pro owns the browser game in `BattleshipLocalPlayActivity`. The existing
X4 Pro / ESP-NOW path returns to `LinkActivity` unchanged. Browser is always
side 1 and places first. This alpha ends after browser fleet confirmation:
X4 fleet setup, shots and rematches are not implemented yet.

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
and profile. Once confirmed, the fleet remains locked; leaving the session on
the X4 starts a new game. There is no takeover or reconnect-to-edit mechanism.

Only these bounded JSON tuples are accepted (token below abbreviated):

```text
["profile","<32 lowercase hex digits>",revision,hair,eyes,mouth]
["place","<32 lowercase hex digits>",revision,shipIndex,bow,horizontal]
["ready","<32 lowercase hex digits>",revision]
```

- Profile: exactly three integers, each 0..13. Name and avatar are derived on
  the X4 through `player::compose` and `player::drawAvatar`, never supplied as
  text or artwork by the browser. Profile is session-only, not saved to SD.
- Place: ship index 0..4, bow 0..99, horizontal 0 or 1. The X4 applies the
  existing core's board and collision checks against the browser's draft.
- Ready: all five ships must form a valid fleet. Only `bship::place(game, 1, …)`
  commits the fleet and passes the turn to the X4. No command can choose a side.
- Revision starts at zero and advances once per accepted action. Stale/duplicate
  revisions, wrong phases and any edits after confirmation are refused.
- Maximum application command is 128 bytes; parsing and DTO serialization have
  no heap allocations. Commands are limited to one per 100 ms. Unknown commands,
  extra/missing values, negative/fractional/coerced integers and overflow are
  refused. Binary and fragmented messages never enter the command dispatcher.

`Server` does not include or accept `bship::Game`. A function-pointer callback
passes a typed command to the activity, which owns `BrowserPlayer` and the game.
The activity protects profile changes against rendering with `RenderLock`.
No new task or per-message application heap allocation is introduced; the
private response buffer is a fixed 256-byte server member.

## Responses and secrecy

The three-field `BrowserSnapshot` remains the public broadcast allowlist:

```json
{"type":"state","phase":"placement","connected":true,"myTurn":false}
```

Only the owner receives `placement` replies with `accepted`, `revision`,
`profile`, `ready`, three validated `slots`, and five `[bow,horizontal]` pairs
from **its own draft**. Bow 255 means that ship has not been placed. Refused
semantic actions return the unchanged owner view. Authentication, ownership,
rate and syntax failures return a generic `error` without private state.

The browser draws only acknowledged positions; it has no placement rules
engine or optimistic fleet state. The renderer's fixed ship lengths describe
only the drawing. Neither serializer can accept a raw game; neither accesses
the X4 fleet, and private replies are never broadcast, even after game over.

Back closes WebSocket, HTTP, DNS and mDNS before the existing network and
nested Developer Mode ownership is released. HTTP/captive-portal probes do
not count as a connected player.

## Verification

`host-tests/battleship/run.sh` covers strict parsing, all 2,744 profiles,
placement bounds/overlap, duplicate revisions, premature/repeated confirmation,
phase/seat checks, projection secrecy across 1,000 enemy fleets, capability
rotation, client-index reuse, private replies, command throttling, nested radio
yields, failed starts, AP/LAN cleanup and browser controls/reconnection.
`host-tests/link/run.sh` covers the unchanged ESP-NOW logic. The simulator
builds the code but retains its disabled hardware-network backend.

On hardware:

1. Games → Battleship → PLAY NEARBY → BROWSER → HOTSPOT. Join
   `CrossPlay-Battleship` and open the QR URL or displayed IP URL in a browser.
2. Choose three profile slots and press **Use player**. Check the derived name
   and matching face on the X4. Place each ship by choosing it, its direction,
   and its starting cell. Out-of-bounds/overlapping moves must leave it unchanged.
3. Confirm all five ships; check **BROWSER FLEET READY**. Edits are now locked.
   This is the end of this alpha's interactive setup, not yet a playable match.
4. Before confirming, close/reopen the owner tab: its draft must be cleared.
   A second simultaneous tab must not change or see the owner's placement.
5. Repeat via BROWSER → WI-FI. With Developer Mode enabled, repeatedly enter
   and leave both modes; its normal service should return after Back. Then
   select X4 PRO and exercise a normal ESP-NOW match on two devices.
