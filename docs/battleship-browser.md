# Battleship browser crossplay

The X4 Pro owns the browser game in `BattleshipLocalPlayActivity`. The existing
X4 Pro / ESP-NOW path returns to `LinkActivity` unchanged. Browser is side 1
and the X4 Pro is side 0. After the browser profile is accepted, both players
may prepare their fleet in parallel. The match starts only after both have
confirmed their fleet. The browser path supports a complete match: profile,
parallel fleet placement, alternating shots, game over, surrender and a
same-session rematch.

## Transport boundary

HTTP serves `/battleship` on port 80; WebSocket uses port 81. The page obtains a
128-bit random session capability through a same-origin, uncached GET to
`/battleship/session`. That endpoint has no CORS allowance. The page is not
frameable. This is local-session/CSRF protection, not account authentication
or encrypted transport; the existing hotspot remains open.

The first accepted profile binds the browser seat to that WebSocket client.
Other clients receive only the public status DTO and cannot issue commands.
A heartbeat detects disconnects. Losing the owner rotates the normal session
capability and clears an unconfirmed profile/draft. A confirmed match can be
resumed by the same browser with a separate resume capability kept in
`sessionStorage`; a different browser cannot take over the seat. Leaving the
browser session on the X4 starts a new game. There is no spectator mode.

Only these bounded JSON tuples are accepted (token below abbreviated):

```text
["profile","<32 lowercase hex digits>",revision,hair,eyes,mouth]
["place","<32 lowercase hex digits>",revision,shipIndex,bow,horizontal]
["randomize","<32 lowercase hex digits>",revision]
["ready","<32 lowercase hex digits>",revision]
["fire","<32 lowercase hex digits>",revision,cell]
["surrender","<32 lowercase hex digits>",revision]
["rematch","<32 lowercase hex digits>",revision]
["resume","<32 lowercase hex digits>",revision,"<resume capability>"]
```

- Profile: exactly three integers, each 0..13. Name and avatar are derived on
  the X4 through `player::compose` and `player::drawAvatar`, never supplied as
  arbitrary text or artwork by the browser. The browser uses the same exported
  avatar layers for its local preview. Profile is session-only, not saved to SD.
- Place: ship index 0..4, bow 0..99, horizontal 0 or 1. The X4 applies the
  existing core's board and collision checks against the browser's draft.
- Randomize: creates a valid browser fleet through the same Battleship core.
- Ready: all five ships must form a valid fleet. Browser readiness and X4
  readiness are tracked independently at the activity layer, allowing both
  placement UIs to be used at the same time. The X4 fleet remains a local draft
  until the browser fleet is committed; once both players are ready, side 0 is
  committed and the normal alternating match starts.
- Fire: cell 0..99. It is accepted only while the browser owns the turn and the
  target has not already been fired on. The browser UI requires one tap to arm
  a target and a second tap on the same cell to fire. `BattleshipCore` remains
  authoritative for hit/miss, sunk state, turn changes and game over.
- Surrender: accepted only during a live match. It ends the round as a browser
  defeat. The browser requires an explicit second confirmation tap before the
  command is sent.
- Rematch: accepted only after game over. It resets both fleets and all shot
  state while keeping the same browser owner and player profile. Both players
  may again place their new fleets in parallel.
- Resume: requires the current session capability, matching revision and the
  separate resume capability. Only a previously confirmed browser fleet is
  resumable; unfinished placement is intentionally discarded.
- Revision starts at zero and advances once per accepted application action.
  Stale/duplicate revisions and wrong-phase commands are refused.
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
to know: phase/turn/winner, the browser's shot results against the X4, the
shots received by the browser fleet and which X4 ships are publicly known to
be sunk. Unknown X4 fleet cells remain unknown. The compact wire form stays
within the server's fixed snapshot buffer.

Only the owner receives `placement` replies with `accepted`, `revision`,
`profile`, `ready`, three validated `slots`, and five `[bow,horizontal]` pairs
from **its own fleet**. Bow 255 means that ship has not been placed. The same
private reply carries the new revision after a shot, surrender or rematch
without exposing host fleet state.

The browser draws only acknowledged placement and public battle information.
No serializer accepts a raw `bship::Game`; the X4 fleet itself is never sent to
the browser. A hit identifies only a cell after the browser has fired there.

## Browser UI

The setup is staged: the fleet controls stay hidden until `USE PLAYER` has been
accepted. The avatar preview uses the same Hair/Eyes/Mouth layers as the reader
and is rendered white on a dark tile for contrast. Placement supports direct
ship selection, double-tap rotation and server-validated random placement.

During the match, setup controls are removed. The enemy board is the primary
board, coordinates are outside its cells, and a target must be selected before
it can be fired. A compact HUD shows both fleet summaries and the browser's own
sea as a minimap. Game over overlays a clear `VICTORY` or `DEFEAT` banner.
`SURRENDER` is available only while a round is live and `PLAY AGAIN` only after
it ends.

Back closes WebSocket, HTTP, DNS and mDNS before the existing network and
nested Developer Mode ownership is released. HTTP/captive-portal probes do
not count as a connected player.

## Verification

`host-tests/battleship/run.sh` covers strict parsing, all 2,744 profiles,
placement bounds/overlap, duplicate revisions, confirmation, turn-checked
firing, two-step browser targeting, duplicate targets, surrender, rematch,
projection secrecy, capability rotation, private replies, throttling, radio
yields, cleanup and browser controls. `host-tests/link/run.sh` covers the
unchanged ESP-NOW logic. The simulator builds the UI path while retaining its
disabled hardware-network backend.

On hardware:

1. Games → Battleship → PLAY NEARBY → BROWSER → HOTSPOT. Join
   `CrossPlay-Battleship` and open the QR URL or displayed IP URL.
2. Choose the browser profile. Verify that the browser fleet controls appear
   only after the profile was accepted and that the X4 can immediately start
   placing its own fleet.
3. Place ships on both devices in either order. If one player confirms first,
   that side must show `WAITING FOR <player>` while the other can keep editing.
   The match must start only after both confirmations.
4. On the browser, tap an enemy cell once and verify it is only selected. Tap
   the same cell again to fire. Check alternating turns, hit/miss markers,
   sunk-ship status, compact own-sea minimap and the final result banner.
5. During a live round, press `SURRENDER` once and verify no command is sent
   until `CONFIRM SURRENDER` is pressed. Verify the round ends as a browser
   defeat and `PLAY AGAIN` becomes available.
6. Start a rematch. The same player profile must remain while both fleet drafts
   and all shots clear; both players can again place in parallel without
   reconnecting or restarting the hotspot.
7. Repeat through BROWSER → WI-FI. With Developer Mode enabled, repeatedly enter
   and leave both modes; its normal service should return after Back. Then
   select X4 PRO and exercise a normal ESP-NOW match on two devices.
