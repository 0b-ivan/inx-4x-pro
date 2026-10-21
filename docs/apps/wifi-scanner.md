# WiFi Scanner

Passive 2.4 GHz access-point scanner ported from
[biscuit](https://github.com/yattsu/biscuit).

## What it does

- Async scan via `WiFi.scanNetworks(true)` — never blocks the render loop.
- Lists every visible AP with SSID, BSSID, RSSI, channel and encryption type.
- Sortable by signal strength or SSID.
- Per-AP signal meter: continuous single-channel targeted rescan at 500 ms
  intervals, min/max/avg stats, 40-sample RSSI history drawn as a bar graph.
- Channel analyser: AP-count bar chart per channel, ±2-channel interference
  scoring, recommended channel (prefers 1 / 6 / 11).
- CSV export to `/crossplay/wifi-scanner/scan_<millis>.csv`.

## Porting notes (biscuit C3 → CrossPlay S3)

### What required no change

- `WiFi.scanNetworks()` / `WiFi.SSID()` / `WiFi.RSSI()` / `WiFi.BSSID()` /
  `WiFi.channel()` / `WiFi.encryptionType()` — identical Arduino-ESP32 API on
  both SoCs.
- `MappedInputManager::Button` enum — CrossPlay carries the same names.
- `ButtonNavigator` — present at `src/util/ButtonNavigator.h`, same interface.
- All scan logic, channel-analysis arithmetic, signal-meter math — pure C++,
  no hardware dependency.

### Adapted for CrossPlay

| biscuit | CrossPlay | Reason |
|---------|-----------|--------|
| `RADIO.ensureWifi()` | `WiFi.mode(WIFI_STA)` | CrossPlay has no RadioManager; HackerNews uses the same direct call ([HackerNewsActivity.cpp:76](../src/apps_local/hackernews/HackerNewsActivity.cpp)) |
| `RADIO.shutdown()` | `WiFi.mode(WIFI_MODE_NULL)` | Symmetric teardown |
| `finish()` on Back | `shelf::leave(renderer, mappedInput)` | CrossPlay shelf navigation contract |
| `Storage.writeFile(path, arduinoString)` | `Storage.writeFile(path, String(...))` | HalStorage API identical; path prefix changed to `/crossplay/wifi-scanner/` |
| `tr()` on UI strings | raw `const char*` | LOCAL_SCOPE.md §Deliberate deviations — no tr() in `src/apps_local/` |
| Class in `src/activities/apps/` | `src/apps_local/wifi-scanner/` | CrossPlay convention: all fork additions in new files under `src/apps_local/` |
| No `create()` factory | `static std::unique_ptr<Activity> create(...)` | Required by the `shelf::Item` registration pattern |

### S3-specific considerations

- **PSRAM**: `std::vector<Network>` allocations sit in PSRAM on S3 (8 MB
  external). The `.reserve(result)` call from biscuit stays; fragmentation on
  this heap is cheap compared to internal DRAM.
- **Dual core**: WiFi stack runs on core 0 by default; the scan callback fires
  from that core. `WiFi.scanComplete()` is the safe polling interface from core
  1 (the Arduino loop core). No change needed.
- **RISC-V alignment rule**: does not apply — S3 is Xtensa LX7. The
  `memcpy`-based BSSID read from biscuit is kept anyway since it is correct
  everywhere.

### Not ported in this phase

The **Offense**, **Defense** and **Comms** tiles from biscuit (deauth,
beacon flood, captive portal, BLE spam, dead drop, …) are out of scope for
CrossPlay. LOCAL_SCOPE.md defines the fork as "games and small tools that make
the device worth carrying instead of a phone" — passive scanning and diagnostics
fit that bar; active transmission tools aimed at third-party networks do not.

The **BLE Scanner** from biscuit is in scope and will follow in a subsequent
PR. It requires `BLEDevice.h` and the bluedroid stack, which conflicts with the
WiFi scan radio in a way that needs a sequencing layer CrossPlay does not yet
have.

## Phase 2 scope (not this PR)

- BLE Scanner (`src/apps_local/ble-scanner/`)
- Wardriving / AP History Logger
- Deauth Detector (passive monitor only)
- Lucide `wifi` icon replacing the `study` placeholder in Shelf.cpp

## Registration

`kApps[]` in `src/apps_local/Shelf.cpp`. Appears after AUTHENTICATOR.
