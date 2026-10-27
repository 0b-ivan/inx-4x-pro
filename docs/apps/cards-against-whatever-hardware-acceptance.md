# Cards Against Whatever — Hardware Acceptance (8 phones, heap, watchdog)

Status: **open**. Required before PR #47 may be merged or any newer CardParty
build is released. Host and SDL tests do not cover radio, phone browsers,
TCP back-pressure, heap under real traffic, watchdog or e-ink behaviour.

The nightly `v1.16.13-rc.20261027.ge3bdea3c` predates all session and slot
hardening. Results from it do **not** count for this plan. Use a build of
the current PR #47 HEAD (local flash, or a nightly explicitly approved for
that exact SHA).

## Equipment

- X4 Pro with the build under test, charged above 60 %.
- 8 phones, at least 2 iOS (Safari) and 2 Android (Chrome). One spare phone
  or laptop browser as the ninth/idle client.
- Laptop with USB-C cable for the serial log:
  `python3 scripts/debugging_monitor.py` or `pio device monitor -b 115200`.
  Save the full log per run (`caw-<date>-<run>.log`).
- Player names, at least one with umlauts at the byte limit, e.g.
  `Jürgen-Schäfer` (16 bytes) and `Zoë`.

## Telemetry

Serial, every 15 s while the server runs:

```
CARDPARTY diag heap=… min=… maxblk=… psram=… ws=… ap=… seats=c/s rej=… takeover=… idle=… sendfail=… reset=…
```

| Field | Meaning |
|---|---|
| `heap` / `min` | free internal heap now / lowest since boot (bytes) |
| `maxblk` | largest allocatable internal block (fragmentation) |
| `psram` | free PSRAM |
| `ws` | open WebSocket slots (max 10) |
| `ap` | Wi-Fi stations associated with the hotspot |
| `seats` | connected / seated players |
| `rej` | rejected browser commands (stale revisions, duplicates, bad tokens) |
| `takeover` | reload takeovers that closed an old socket |
| `idle` | unauthenticated sockets closed after 20 s |
| `sendfail` | state frames not delivered or over the 6 KB bound |
| `reset` | `esp_reset_reason`: 1 power-on, 3 software, 4 panic, 5 int-WDT, 6 task-WDT, 7 other WDT, 9 brownout |

X4 footer (repainted only with normal game updates): `SYS H<heap>K M<min>K WS<n> AP<n>`.
The server also logs `Game server ready … (reset reason N)` on start.

A reboot during a run is visible as a new boot banner plus `reset=` ≠ 1 at
the next start. Any reboot fails the run.

## Pass thresholds

| Metric | Pass |
|---|---|
| Reboots / WDT / panic | 0 |
| `min` free heap | ≥ 50 KB at all times |
| Heap leak | `heap` in the lobby after a rematch within 8 KB of the lobby value with the same 8 phones before game 1 |
| `maxblk` | ≥ 16 KB at all times |
| `sendfail` | 0 in T1–T6; only during T8 (dead phone) acceptable |
| `ws` | ≤ 10, returns to number of connected seats within 25 s of any reload/idle event |
| Private data | no phone ever shows another phone's hand or the authors before Reveal |

## Test cases

Record every case in the result table below. Start a fresh serial log per run.

**T1 — Join 8 via hotspot.** X4: CARDS WHATEVER → `[01] CREATE HOTSPOT`.
Each phone scans QR 01 (Wi-Fi), then QR 02 (game) and joins.
Pass: lobby shows 8 connected, `seats=8/8`, `ws=8`, `ap=8`. Note iOS
behaviour: if a captive-portal sheet opens instead of Safari, the game will
not connect there (Origin mismatch) — record it; the workaround is opening
QR 02 in Safari.

**T2 — Ninth player and idle tab.** Spare client joins Wi-Fi and opens the
game. Join → `ERROR: SESSION FULL`. Leave it on the join screen.
Pass: after ~20 s `idle` +1, `ws` back to 8, spare shows
`IDLE / JOIN TO CONNECT` and does not reconnect by itself.

**T3 — Ghost seat.** Before starting: one player opens the QR in a
different browser and joins again with a new name, then closes the first
browser tab. Pass: once the lobby is full, a new join takes the
disconnected seat; no connected player is removed.

**T4 — Ten full rounds, 8 players.** Start on the X4. Play all 10 rounds.
In at least one round each: Pick 2, Pick 3 (Draw 2 prompt), Rando wins,
Czar rotation visible on X4. Pass: 10 rounds, scores add up (one point per
human win, none for Rando), no stuck round, thresholds held.

**T5 — Reload storm.** During rounds 2–4: every phone reloads the page
at least twice — once while choosing, once after submitting; the Czar
reloads during Judging, someone during Reveal. Pass: every phone returns to
its seat with the same hand, submissions are not lost or doubled, `takeover`
increases, `ws` returns to 8.

**T6 — Phone lock (held turn).** Last pending answerer locks the phone for
~10 s, unlocks and answers. Pass: round waits, X4 shows `n-1 / n`, answer is
accepted. Repeat with the phone locked for > 30 s: round advances to Judging
without that player; on unlock the phone shows the current phase.

**T7 — Czar drop.** Czar turns Wi-Fi off during Judging for 15 s, back on.
Pass: Czar resumes and judges. Repeat with Czar gone > 60 s: X4
`[ ABORT STALLED ROUND ]` needs two taps and moves on.

**T8 — Half-dead phone.** Mid-round, one phone enters airplane mode *without*
closing the page. Pass: no reboot; the other phones keep receiving updates
(note the longest visible stall, expected ≤ 15 s until the heartbeat drops
the dead socket); `seats` drops to 7/8. This is the main risk the host tests
cannot model: `sendTXT` to a socket with a full TCP window blocks the loop.

**T9 — Stray BACK.** Press BACK once during a round. Pass: X4 shows
`PRESS BACK AGAIN TO END SESSION`, game continues. Second press within 7 s
ends the session.

**T10 — Rematch.** At GAME OVER tap `[ REMATCH ]`. Pass: all phones land in
the lobby without rejoining Wi-Fi or rescanning, scores 0, same names; start
and play 3 rounds. Heap leak threshold checked here.

**T11 — Existing Wi-Fi mode.** `[02] EXISTING WI-FI`, 3+ phones on the same
network. Pass: join and one round. Note: unencrypted `ws://` on a shared LAN
is not accepted as secure; only use a trusted network.

**T12 — Soak.** Leave 8 phones connected in the lobby or a running round for
60 min (screens may sleep). Pass: no reboot, `min` stable (no monotonic
decline across the hour), phones resume on wake.

## Browser checks (any phone, during T4)

320 px (small Android or devtools), 390 px, desktop: long German prompt,
Pick 3 order markers visible, sticky `[ SUBMIT ]` reachable with keyboard
closed and open, scroll position kept when selecting.

## Result table

| Case | Date | Build SHA | Phones (iOS/Android) | Result | min heap | max `ws` | Notes |
|---|---|---|---|---|---|---|---|
| T1 | | | | | | | |
| T2 | | | | | | | |
| T3 | | | | | | | |
| T4 | | | | | | | |
| T5 | | | | | | | |
| T6 | | | | | | | |
| T7 | | | | | | | |
| T8 | | | | | | | |
| T9 | | | | | | | |
| T10 | | | | | | | |
| T11 | | | | | | | |
| T12 | | | | | | | |

Attach the serial logs to the PR. A failed case blocks the merge until it is
fixed and re-run on the new SHA.
