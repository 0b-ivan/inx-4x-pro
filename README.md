# Inx for Xteink X4 Pro

Experimental port of [Inx](https://github.com/obijuankenobiii/inx) to the **Xteink X4 Pro**.

> [!WARNING]
> **This is still experimental firmware. Do not use PlatformIO's generic upload command.**
> Builds must be installed only through the guarded inactive-OTA-slot workflow in
> [`docs/X4PRO_FLASHING_GUIDE.md`](docs/X4PRO_FLASHING_GUIDE.md). The workflow has been validated on a real X4 Pro,
> including full-flash backup, readback verification, test boot and rollback to the factory slot.

This fork targets the X4 Pro specifically:

- ESP32-S3
- 16 MiB flash
- 8 MiB PSRAM
- 800x480 e-paper display
- GT911 capacitive touch
- two physical navigation keys
- capacitive Home key
- 1-bit SDMMC microSD
- CW2017 battery gauge
- warm/cold frontlight hardware

Hardware definitions come from the pinned **FreeInk** X4 Pro board profile instead of hard-coded GPIO numbers in Inx.

## Project status

| Area | Status |
| --- | --- |
| ESP32-S3 build | ✅ builds in CI |
| FreeInk X4 Pro board profile | ✅ integrated |
| Physical-device boot and display | ✅ validated |
| Display controller detection | ✅ validated |
| PSRAM | ✅ build configured |
| SDMMC | ✅ backend integrated |
| Physical side buttons | ✅ mapped |
| GT911 touch orientation | ✅ physically calibrated |
| Direct touch UI | ✅ reader, library, recent, statistics, settings and device menus |
| Capacitive Home key | ✅ mapped to Back / Menu |
| Quick Settings | ✅ touch drawer with persistent controls |
| Frontlight | ✅ warm/cold control, persistence and sleep handling |
| Night mode | ✅ persistent display inversion |
| German UI | ✅ integrated |
| Tarot app and sleep screen | ✅ integrated with verified asset downloads |
| Battery | 🧪 backend integrated, hardware validation pending |
| Deep sleep / wake | 🧪 basic hardware behavior confirmed; extended validation pending |
| RTC clock UI | 🧪 available when a working RTC is detected |
| X4 Pro OTA updater | 🧪 enabled for application-only alpha releases |
| Generic PlatformIO upload | 🔒 blocked intentionally |
| Guarded inactive-slot flash and rollback | ✅ validated on hardware |

## Safety model

The X4 Pro has flash memory containing much more than the Inx application. A careless full-device flash can overwrite the bootloader, partition table, factory calibration or the only known-good application.

For this port the rules are therefore:

1. **Never erase the complete flash for a normal test.**
2. **Never overwrite the bootloader.**
3. **Never overwrite the live partition table.**
4. **Never erase NVS or `otadata` during a normal test.**
5. **Never use `pio run -t upload`.** The project blocks this on purpose.
6. **Only install this repository's X4 Pro releases.** The updater accepts the application-only `firmware.bin`; never flash bootloader or partition files.
7. Before the first write, read the real device partition table.
8. Before the first write, create a complete 16 MiB flash backup and SHA-256 checksum.
9. Testing must write only an **inactive OTA application slot** through the guarded helper.
10. Keep the known-good application slot intact until Inx has passed boot, display, input, SD, sleep and restart tests.

The detailed explanation is in [`docs/X4PRO_FLASHING_GUIDE.md`](docs/X4PRO_FLASHING_GUIDE.md).

Technical port status is documented in [`docs/X4PRO_PORT.md`](docs/X4PRO_PORT.md).

## Why an inactive OTA slot?

A simplified X4 Pro flash layout looks conceptually like this:

```text
ESP32-S3 flash

+----------------------------+
| Bootloader                 |  keep
+----------------------------+
| Partition table            |  keep
+----------------------------+
| NVS / device data          |  keep
+----------------------------+
| OTA metadata               |  keep
+----------------------------+
| Application slot A         |  known-good firmware
+----------------------------+
| Application slot B         |  experimental Inx
+----------------------------+
| Other device data          |  keep
+----------------------------+
```

The exact offsets are **not assumed**. They are read from the physical device before the first test.

## Build

### Requirements

- macOS, Linux or Windows
- Python 3
- PlatformIO Core
- Git

On macOS:

```bash
python3 -m pip install --upgrade platformio esptool
```

Clone the repository including submodules:

```bash
git clone --recursive https://github.com/0b-ivan/inx-4x-pro.git
cd inx-4x-pro
git submodule update --init --recursive
```

Build:

```bash
pio run -e x4pro
```

Application image:

```text
.pio/build/x4pro/firmware.bin
```

A successful build means only that the program can be compiled for the ESP32-S3 target. It is **not** permission to bypass the guarded flashing procedure.

## Read-only hardware preflight

Before any write to the X4 Pro, connect it over USB and identify the serial port.

macOS:

```bash
ls /dev/cu.*
```

Then run:

```bash
python3 scripts/x4pro_inspect.py \
  --port /dev/cu.usbmodemXXXX \
  --firmware .pio/build/x4pro/firmware.bin \
  --backup
```

The inspector intentionally contains **no flash write, erase or boot-selection command**. It:

- verifies that the connected MCU is an ESP32-S3;
- reads the live partition table from flash;
- validates that multiple OTA application slots exist;
- checks that `firmware.bin` fits those slots;
- optionally reads the complete 16 MiB flash;
- creates a SHA-256 checksum of that backup.

Expected backup files:

```text
x4pro-preflight/
├── partition-table.bin
├── x4pro-full-16mb.bin
└── x4pro-full-16mb.bin.sha256
```

The backup directory is intentionally ignored by Git because it contains device-specific flash data and may contain credentials.
After preflight, follow the staging and boot-selection steps in the flashing guide. The helpers validate the live partition
layout, security state, backup consistency, inactive target slot and post-write readback before allowing a boot switch.

## Highlights

### Touch-first X4 Pro interface

The original button-oriented screens now have a semantic touch layer. Taps are mapped into logical UI coordinates and
dispatched to the active screen or modal dialog. This enables direct interaction with:

- main tabs, library controls and book lists;
- EPUB page-turn and reader-menu zones;
- recent books, saved words and statistics;
- settings rows, selectors and device-management actions;
- nested activities and confirmation dialogs.

Four-direction swipe navigation remains available where a screen uses list or content navigation. A swipe from the top
edge opens the Quick Settings drawer.

### Quick Settings, frontlight and night mode

The CrossPoint-style Quick Settings drawer provides fast access to reader touch, display inversion/night mode and the
X4 Pro frontlight. Warm and cold light levels, on/off state and quick-control preferences are persisted and restored after
boot. Frontlight PWM is kept alive during light sleep where required by the hardware.

### Tarot

The Sync/Tools page includes a complete 78-card Tarot activity with card meanings, draw history, touch controls and a
Tarot standby screen. If the deck is missing, the reader can download the manifest and assets over Wi-Fi, verify every
file by size and SHA-256, and install them to `/tarot/` on the SD card. The same files can be copied manually from the
repository's [`tarot/`](tarot/) directory.

### Display, sleep and localization

- FreeInk performs runtime display-controller detection and panel output inversion.
- Sleep screens support Tarot, light/dark screens, recent-book covers, transparent covers, custom BMP/JPEG images and
  an RTC-backed date/time screen when the board exposes a working clock.
- Custom sleep images can be selected individually or randomized from `/sleep/`.
- English and German UI languages are available in Settings.
- Reader quick actions, presets, external SD-card fonts and selectable image quality remain available from upstream Inx.

## Input mapping

The semantic touch layer handles direct hit-testing on adapted screens. A compatibility mapping remains for screens that
still use Inx's original button-oriented input model:

| X4 Pro input | Inx action |
| --- | --- |
| side key 1 | Up / previous |
| side key 2 | Down / next |
| direct screen tap | Activate the touched control where supported |
| fallback screen tap | Confirm |
| capacitive Home tap | Back / Menu |
| horizontal swipe | Left / Right |
| vertical swipe | Up / Down |
| top-edge swipe | Open Quick Settings |
| power button | Power |

## Hardware validation checklist

The first hardware test is intentionally boring. We are not testing every Inx feature at once.

```text
[x] ESP32-S3 boots the experimental application
[ ] serial log remains available
[x] e-paper controller is detected
[x] display initializes without BUSY lockup
[x] screen orientation is correct
[x] side buttons work
[x] touch tap and corner calibration work
[x] Home key works
[ ] four-direction swipe behavior is fully validated
[ ] SD card mounts
[ ] EPUB can be opened
[ ] battery value is plausible
[x] basic sleep and power-button wake work
[ ] restart works
[x] known-good OTA slot remains recoverable
```

The checklist distinguishes implemented software from completed physical-device validation. See
[`docs/X4PRO_PORT.md`](docs/X4PRO_PORT.md) for the detailed engineering status.

## Inx features

The port keeps the Inx application layer, including EPUB/TXT/Markdown reading, library browsing, bookmarks, annotations,
dictionary lookup, KOReader sync, OPDS, Calibre integration, image rendering, SD-card fonts, reader presets and quick
actions, sleep screens, statistics, backup/restore and the local web interface.

Some features can remain temporarily unavailable while the X4 Pro hardware layer is being validated.

## Development

Useful targets:

```bash
# X4 Pro firmware
pio run -e x4pro

# SDL simulator
CROSSPOINT_SIM_SD=./fs_ pio run -e simulator -t run_simulator

# web-only simulator
CROSSPOINT_SIM_SD=./fs_ pio run -e simulator_web -t run_simulator
```

### Important: upload is intentionally blocked

This is expected to fail:

```bash
pio run -e x4pro -t upload
```

That failure is a safety feature, not a bug.

## Documentation

- [`docs/X4PRO_FLASHING_GUIDE.md`](docs/X4PRO_FLASHING_GUIDE.md) — beginner-friendly explanation of bootloader, partitions, OTA slots, backup, flashing, rollback and the planned first test.
- [`docs/X4PRO_PORT.md`](docs/X4PRO_PORT.md) — technical implementation status and port constraints.

## Upstream

This repository is based on Inx and uses FreeInk hardware support for the Xteink X4 Pro.

It is a community project and is not affiliated with Xteink.
