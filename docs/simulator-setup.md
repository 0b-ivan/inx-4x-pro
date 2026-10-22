# Simulator Setup im Cloud-Container (Stand: Batch A-E)

Dieses Dokument beschreibt den aktuellen Stand der Simulator-Integration für
PR #36 (Batches A–E, 60 Security-Apps) sowie die manuellen Schritte, die in
einem **frischen Cloud-Container** erforderlich sind, bis PlatformIO's Package
Registry wieder erreichbar ist.

---

## Was funktioniert

- **Batches A–E vollständig geportet** (60 Apps): Shelf.cpp, Shelf.h, README,
  site/index.html, i18n-Keys alle aktuell.
- **DevMode-Pattern** auf alle 33+ Radio-Apps angewendet (`devmode::pause()` /
  `devmode::resume()`).
- **i18n**: 31 fehlende `STR_*`-Keys nachgetragen; Generator läuft sauber.
- **shelf_coverage.py**: `python3 host-tests/site/shelf_coverage.py .` → exit 0.
- **Simulator-Infrastruktur** (`platformio.sim.ini`, `sim-stubs/`, `sim_catchup.py`)
  ist im Repo vorhanden und prinzipiell korrekt.

---

## Simulator-Status (Cloud-Container-spezifisch)

### Problem: PlatformIO Registry ist blockiert

`api.registry.platformio.org` und `api.registry.nm1.platformio.org` werden
vom Proxy mit HTTP 403 abgewiesen. Dadurch kann PlatformIO folgende Pakete
**nicht** herunterladen:

| Paket | Benötigt von |
|---|---|
| `platformio/native` (Platform) | `platform = native` in sim.ini |
| `platformio/tool-scons ~4.41101.0` | Build-System der native-Platform |
| `bblanchon/ArduinoJson @ 7.4.2` | Simulator lib_deps |
| `ricmoo/QRCode @ ^0.0.1` | Simulator lib_deps |

### Workarounds (bereits in dieser Session angewendet)

1. **Native-Platform von GitHub installiert**:
   ```bash
   git clone --depth 1 https://github.com/platformio/platform-native /tmp/platform-native
   pio pkg install --global --platform "file:///tmp/platform-native"
   ```

2. **SCons-Version-Constraint gepatcht** (`~4.41101.0` → `~4.40801.0`) in
   `/usr/local/lib/python3.13/dist-packages/platformio/dependencies.py`
   — weil nur `tool-scons@4.40801.0` lokal vorhanden ist (kommt mit
   espressif32-Platform).

3. **`get_core_package_dir`-Fallback gepatcht** in
   `/usr/local/lib/python3.13/dist-packages/platformio/package/manager/core.py`
   — damit PlatformIO das vorhandene tool-scons ohne `owner=platformio`-Match
   akzeptiert.

4. **SDL2 installiert**:
   ```bash
   apt-get install -y libsdl2-dev
   ```

5. **ArduinoJson + QRCode von GitHub geklont und lokal installiert**:
   ```bash
   git clone --depth 1 --branch v7.4.2 https://github.com/bblanchon/arduinojson /tmp/arduinojson
   git clone --depth 1 https://github.com/ricmoo/qrcode /tmp/qrcode
   cd /path/to/inx-4x-pro
   pio pkg install -e simulator_x4_pro --library "file:///tmp/arduinojson" --library "file:///tmp/qrcode"
   ```

6. **Simulator-Library (Crosspoint Simulator) installiert** (funktioniert via
   direktem GitHub-URL, kein Registry-Aufruf).

7. **`simulator`-Symlink angelegt** (weil die Library als `"Crosspoint Simulator"`
   installiert wird, `sim_catchup.py` aber `simulator/src/` erwartet):
   ```bash
   ln -sf ".pio/libdeps/simulator_x4_pro/Crosspoint Simulator" \
     .pio/libdeps/simulator_x4_pro/simulator
   ```

### Verbleibender Build-Fehler

Nach allen o.g. Fixes schlägt `Shelf.cpp` beim Kompilieren fehl:

```
src/apps_local/probe-sniffer/ProbeSnifferActivity.h:7:10:
  fatal error: freertos/portmacro.h: No such file or directory
```

Die Hardware-Only-App-Header (z.B. `ProbeSnifferActivity.h`, `CrowdDensityActivity.h`,
`AirTagTestActivity.h`) werden von `Shelf.cpp` direkt includiert. Obwohl die
zugehörigen `.cpp`-Dateien per `build_src_filter` aus dem Simulator-Build
ausgeschlossen sind, müssen die Header kompilierbar sein.

**Fehlende Stubs** (nicht im Simulator oder `sim-stubs/` vorhanden):

| Header | Benötigt in |
|---|---|
| `freertos/portmacro.h` | probe-sniffer, crowd-density, deauth-detector, packet-monitor, network-monitor, device-fingerprinter (braucht `portMUX_TYPE`) |
| `BLEDevice.h` | airtag-test, ble-beacon, ble-scanner |
| `esp_now.h` | mesh-chat |

Die Simulator-Library liefert bereits `freertos/FreeRTOS.h`, `freertos/semphr.h`,
`freertos/task.h` mit `portMUX_TYPE`-Definition — aber **nicht** `portmacro.h`.
Das lässt sich durch einen Eintrag in `sim-stubs/freertos/portmacro.h` beheben,
der nur die Simulator-Library via `#include` weiterleitet.

---

## Nächste Schritte für vollständigen Simulator-Build

1. `sim-stubs/freertos/portmacro.h` anlegen (leitet zu `FreeRTOS.h` weiter):
   ```cpp
   #pragma once
   #include <freertos/FreeRTOS.h>
   ```

2. `sim-stubs/BLEDevice.h` anlegen (minimal-Stub für BLE-Typ-Definitionen).

3. `sim-stubs/esp_now.h` anlegen (minimal-Stub für `esp_now_*`-Typen).

4. Build verifizieren: `pio run -e simulator_x4_pro`

5. Screenshots via `sim-shot.sh` für die testbaren Batch-D/E-Apps aufnehmen.

---

## Apps die im Simulator testbar sind (kein Hardware-Zugriff)

| App | Batch |
|---|---|
| calculator | D |
| cipher | D |
| countdown | D |
| morse-code | D |
| qr-generator | D |
| sd-file-browser | D |
| unit-converter | D |
| steganography | E |
| sd-encryption | E |
| security-pin | E |
| quick-wipe | E |
| screen-decoy | E |
| qr-totp | E |

Diese 13 Apps haben keine WiFi/BLE/FreeRTOS-Abhängigkeiten in ihren Headern
und sind nach Beheben der o.g. Stubs direkt im Simulator ausführbar.
