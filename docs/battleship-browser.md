# Battleship browser state preview

The X4 Pro owns the browser session in `BattleshipLocalPlayActivity`. The
existing X4 Pro / ESP-NOW path still returns to `LinkActivity` unchanged.
The browser session starts with a reset game and does not yet accept placement,
shots or rematch commands from either browser controls or WebSocket messages.
On hardware this slice therefore reaches `placement`, not a playable match.
`waiting`, `playing`, `finished` and turn changes are covered by host tests for
the projection and browser UI; later game controls can use the same channel.

## Read-only protocol

HTTP serves `/battleship` on port 80. The dedicated WebSocket server uses port
81 and the page connects to `/battleship` on that port. It sends a snapshot on
connection and broadcasts when the projected state changes. The server and
activity run this work in their existing loop, without another task or lock.
A heartbeat detects disconnected peers. HTTP/captive-portal probes do not
count as a connected browser. Back closes WebSocket, HTTP, DNS and mDNS before
the existing Wi-Fi and Developer Mode ownership is released.

```json
{"type":"state","phase":"placement","connected":true,"myTurn":false}
```

`BrowserSnapshot` is an explicit allowlist of three fields: `phase`, `connected`
and `myTurn`. It has no fleet, board, pointer or raw game bytes. Only this DTO
is accepted by the serializer and transport. Browser is side 1; `myTurn` is
true only during play on that side's turn. No hidden positions are exposed,
even after a game finishes. Text, binary and fragmented application messages
are ignored. The browser reconnects after disconnection and clears stale UI.

The DTO occupies three bytes and serialization uses a bounded 128-byte buffer.
The WebSocket server is a member of the existing activity-owned server; there
is no per-frame application heap allocation or additional worker task.

## Test on an X4 Pro and phone

1. Install the pre-release **X4 Pro** firmware. `firmware.bin` is the application
   image for the existing updater. The `*-x4pro-full.bin` image is for a full
   USB installation at address `0x0`; do not flash the application at `0x0`.
2. Open Games → Battleship → PLAY NEARBY → BROWSER → HOTSPOT.
3. Join `CrossPlay-Battleship` on the phone (stay connected despite the lack of
   internet). Open the game QR or `http://crossplay.local/battleship` in a normal
   browser. If hostname resolution fails, use the displayed IP URL.
4. Expect `BROWSER CONNECTED` on the X4 Pro and `Connected to X4 Pro`,
   `placement`, and the read-only notice on the phone. There are no move buttons.
5. Close all browser tabs. The device should return to `WAITING FOR BROWSER`
   (abrupt loss can take roughly 15 seconds for the heartbeat). Reopen the page:
   connection and state should return without restarting the session.
6. Open a second tab; closing only one must keep the device connected. Leaving
   the browser session with Back must disconnect both tabs and stop the hotspot.
7. Repeat through BROWSER → WI-FI, with the phone on the same local network as
   the X4 Pro. Use the displayed IP if mDNS is unavailable.
8. With Developer Mode enabled, repeat entry/Back several times and verify that
   its usual address works again after leaving. Then select X4 PRO and check a
   normal ESP-NOW match with another device.

Automated checks: `host-tests/battleship/run.sh` tests the actual server against
network fakes, projection secrecy across 1,000 enemy fleets, phases, turn,
bounded serialization, change-only pushes, ignored messages, multiple clients,
restart/cleanup and browser-script reconnection. `host-tests/link/run.sh` covers
the unchanged multiplayer transport. The simulator builds this code but retains
the existing disabled hardware-network backend, so it cannot replace radio tests.
