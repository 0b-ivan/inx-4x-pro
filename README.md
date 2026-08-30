# Inx for Xteink X4 Pro

Experimental port of [Inx](https://github.com/obijuankenobiii/inx) to the **Xteink X4 Pro**.

> [!WARNING]
> **Do not flash the current alpha build yet.**
> The firmware builds successfully for ESP32-S3, but the first real-device flash is deliberately gated behind a read-only hardware inspection and a complete flash backup.

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
| Display controller detection | ✅ integrated |
| PSRAM | ✅ build configured |
| SDMMC | ✅ backend integrated |
| Physical side buttons | ✅ mapped |
| GT911 touch | 🧪 compatibility mapping |
| Capacitive Home key | 🧪 mapped to Back |
| Battery | 🧪 backend integrated, hardware validation pending |
| Deep sleep / wake | 🧪 backend integrated, hardware validation pending |
| Frontlight UI | ❌ not integrated yet |
| RTC UI/integration | ❌ not integrated yet |
| Inx OTA updater | 🔒 disabled intentionally |
| Generic PlatformIO upload | 🔒 blocked intentionally |
| First hardware flash | ⛔ not approved yet |

## Safety model

The X4 Pro has flash memory containing much more than the Inx application. A careless full-device flash can overwrite the bootloader, partition table, factory calibration or the only known-good application.

For this port the rules are therefore:

1. **Never erase the complete flash for a normal test.**
2. **Never overwrite the bootloader.**
3. **Never overwrite the live partition table.**
4. **Never erase NVS or `otadata` during a normal test.**
5. **Never use `pio run -t upload`.** The project blocks this on purpose.
6. **Never use the upstream Inx OTA updater on the X4 Pro.** It is compiled out.
7. Before the first write, read the real device partition table.
8. Before the first write, create a complete 16 MiB flash backup and SHA-256 checksum.
9. First testing must write only an **inactive OTA application slot**.
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

Clone the port branch including submodules:

```bash
git clone --recursive https://github.com/0b-ivan/inx-4x-pro.git
cd inx-4x-pro
git checkout x4pro-port
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

A successful build means only that the program can be compiled for the ESP32-S3 target. It is **not** permission to flash it.

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

**Stop after this step for the first device.** The actual inactive-slot write procedure will only be documented as approved after the real partition layout and recovery path have been validated.

## Input mapping during the port

Inx is currently button-oriented. The X4 Pro has only two discrete navigation buttons plus GT911 touch, so the HAL temporarily maps the hardware into the existing Inx input model:

| X4 Pro input | Inx action |
| --- | --- |
| side key 1 | Up / previous |
| side key 2 | Down / next |
| screen tap | Confirm |
| capacitive Home tap | Back |
| horizontal swipe | Left / Right |
| power button | Power |

Native touch hit-testing is planned later.

## What will be tested on the first real boot?

The first hardware test is intentionally boring. We are not testing every Inx feature at once.

```text
[ ] ESP32-S3 boots the experimental application
[ ] serial log remains available
[ ] e-paper controller is detected
[ ] display initializes without BUSY lockup
[ ] screen orientation is correct
[ ] side buttons work
[ ] touch tap works
[ ] Home key works
[ ] horizontal swipe works
[ ] SD card mounts
[ ] EPUB can be opened
[ ] battery value is plausible
[ ] sleep works
[ ] power-button wake works
[ ] restart works
[ ] known-good OTA slot remains recoverable
```

Only after those tests pass do we expand hardware features such as frontlight and RTC integration.

## Inx features

The port keeps the Inx application layer, including EPUB/TXT/Markdown reading, library browsing, bookmarks, annotations, dictionary lookup, KOReader sync, OPDS, Calibre integration, image rendering, SD-card fonts, sleep screens, statistics and the local web interface.

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
