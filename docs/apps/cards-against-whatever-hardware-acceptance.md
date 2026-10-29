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
CARDPARTY diag heap=… min=… maxblk=… psram=… ws=… ap=… seats=c/s rej=… takeover=… idle=… sendfail=… defer=… drop=… reset=…
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
| `defer` | state frames held back because the phone's TCP send buffer was full (sent later as one current frame) |
| `drop` | sockets closed by the heartbeat: 2 unanswered pings, or send buffer full for 15 s |
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
| `drop` | 0 in T1–T5 and T9–T11; +1 per dead phone in T6 (> 15 s lock), T7, T8 |
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
accepted. Repeat with the phone locked for > 15 s: every screen shows
`WAITING FOR <name>` until the phone is back or the X4 taps
`[ GO ON WITHOUT ]` (from this milestone on; before, the round moved on by itself
after 30 s).

**T7 — Czar drop.** Czar turns Wi-Fi off during Judging for 15 s, back on.
Pass: Czar resumes and judges. Repeat with Czar gone > 60 s: X4
`[ ABORT STALLED ROUND ]` needs two taps and moves on.

**T8 — Half-dead phone.** Mid-round, one phone enters airplane mode *without*
closing the page. Every write to a socket with a full send buffer blocks on
the X4 (the ESP32 client retries `select()` for up to ~10 s), so the server
checks writability first: it defers state frames, pings only writable
sockets, and drops a stuck socket without a close frame.
Pass: no reboot; the other phones keep receiving updates with **no visible
stall** (submit on another phone shows on all others within ~1 s); `defer`
rises; within ~20 s `drop` +1 and `seats` drops to 7/8; X4 buttons stay
responsive throughout. Turn airplane mode off: the phone resumes its seat.

**T9 — Stray BACK.** Press BACK once during a round. Pass: X4 shows
`PRESS BACK AGAIN TO END SESSION`, game continues. Second press within 7 s
ends the session.

**T10 — Rematch.** At GAME OVER tap `[ REMATCH ]`. Pass: all phones land in
the lobby without rejoining Wi-Fi or rescanning, scores 0, same names; start
and play 3 rounds. Heap leak threshold checked here.

**T11 — Existing Wi-Fi mode.** `[02] EXISTING WI-FI` with dev mode off:
the system network picker opens (saved networks connect directly), then the
lobby. 3+ phones on the same network. Pass: join and one round; after
leaving the app the X4's Wi-Fi is off again. Note: unencrypted `ws://` on a shared LAN
is not accepted as secure; only use a trusted network.

**T13 — SD pack and categories.** Copy a released format-2 pack to
`/games/cardparty/active.dat`. Pass: setup shows the deck's title under
`[03] DECK`, its license at the bottom, and `[04] CATEGORIES`; FSK 18 is off on first entry; switching a
category changes the prompt/answer count; the choice survives leaving the app
and a reboot; with too few categories on, setup refuses to start and says
why; a round never shows a card from a switched-off category. Serial log
shows `Loaded released SD deck v<deck version> (…)`; note `heap` right after loading.

**T14 — Deck picker.** Copy two or more released packs into
`/games/cardparty/packs/`, plus one file that is not a pack (e.g. a renamed
text file). Pass: `[03] DECK` lists the built-in demo and every `.dat` by
title with its counts, the non-pack as `NOT A CAW PACK`; tapping a pack
returns to setup with that deck and its categories; tapping the non-pack
shows `NOT LOADED` and keeps the current deck; the choice survives leaving
the app and a reboot; categories switched in one pack are still as set after
switching to another pack and back. Note `heap` after each switch.

**T15 — Download from GitHub.** Card without `/games/cardparty/packs/`.
`[03] DECK` → `[+] Download from GitHub`, pick a network. Pass: the list
shows `Deutsch - alle Karten`; tapping it returns to setup with that deck
and `INSTALLED: …`; the file is on the card, no `.part`/`.bak` left; Wi-Fi is
off after leaving the app. Repeat with Wi-Fi out of range: a clear message,
no crash, the current deck unchanged. Note `heap` before and after.

**T12 — Soak.** Leave 8 phones connected in the lobby or a running round for
60 min (screens may sleep). Pass: no reboot, `min` stable (no monotonic
decline across the hour), phones resume on wake.

**T16 — Pause.** Mid-round, phone A taps `[ PAUSE ]`. Pass: every phone and
the X4 show `PAUSED BY A` and the clock stands still; phone B has no
continue button and its answer is not accepted; A continues, the clock runs
on from where it stood. Repeat with B pausing and the X4 continuing
(`[ CONTINUE ]` or MENU → CONTINUE).

**T17 — Missing player.** Answer timer 60 s. One phone that still has to
answer goes to airplane mode. Pass: after 15 s every phone and the X4 show
`WAITING FOR <name>`, the clock stands still; the phone coming back lifts it
by itself. Again, and the X4 taps `[ GO ON WITHOUT ]`: the round goes on.
Same with the Juror while judging: the round ends without points. A phone
that is not needed (already answered) never blocks.

**T18 — Clocks and auto mode.** Rules: answer 30 s, extension 10 s, Juror
30 s, auto 8 s. Pass: phones count down per second, the X4 in 10 s steps;
with answers missing the clock gets `+10 s` once, then submissions close;
nobody answering ends the round without points; the Juror clock running out
ends the round without points; auto mode starts the next round 8 s after the
reveal, never after the last one. MENU → `+ TIME` adds 10 s.

**T19 — Remove and end.** Remove an answerer (MENU → PLAYERS, tap twice):
their phone shows the removed screen, the round goes on; they can join the
next lobby. Remove the Juror while judging: round ends without points. Remove
until two are left: the game ends with the ceremony. MENU → END GAME (tap
twice): every phone shows `GAME ENDED BY THE HOST`, crowns for all leaders,
`[ AGAIN! ]` votes count on the X4; `[ AGAIN ]` opens the lobby with the same
seats, `[ END ]` returns to setup.

**T20 — Changes during a match.** Switch a category off mid-round: new cards
come without it, cards already dealt stay; a switch that leaves too few cards
is refused. In the reveal, MENU → DECK → another pack: everyone gets a new
hand, points stay. Rando off: from the next round there is one answer fewer.
Note `heap` after the pack switch.

## Browser checks (any phone, during T4)

Chromium layout at 320×568, 390×844 and 1280×800 is checked in CI by
`host-tests/cardparty/test_browser_viewport.cjs` (no horizontal scroll, no
clipped or overlapping cards, SUBMIT/CONFIRM on screen, Pick 3 order and
hand scroll position kept; screenshots in the `cardparty-browser-viewports`
artifact). What only real phones show:

- iOS Safari: bottom toolbar and safe area over the sticky `[ SUBMIT ]`,
  keyboard on the join screen, rubber-band scrolling inside the hand list.
- Android Chrome: same, plus system font scaling at 130 %.
- Long German prompt, Pick 3 order markers visible, scroll position kept
  when selecting — by touch, not mouse.

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
| T13 | | | | | | | |
| T14 | | | | | | | |
| T15 | | | | | | | |
| T16 | | | | | | | |
| T17 | | | | | | | |
| T18 | | | | | | | |
| T19 | | | | | | | |
| T20 | | | | | | | |

Attach the serial logs to the PR. A failed case blocks the merge until it is
fixed and re-run on the new SHA.
