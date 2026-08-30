# Inx X4 Pro port — technical status

> [!WARNING]
> **Experimental. The current alpha is not yet approved for hardware flashing.**

For a beginner-friendly explanation of bootloaders, partitions, OTA slots, backups and the planned first hardware test, read [`X4PRO_FLASHING_GUIDE.md`](X4PRO_FLASHING_GUIDE.md).

## Goal

Port Inx 1.0.19 from the original Xteink hardware target to the **Xteink X4 Pro** while keeping the first hardware test reversible.

The port targets:

- ESP32-S3;
- 16 MiB flash;
- 8 MiB OPI PSRAM;
- 800x480 E-Ink;
- GT911 touch;
- X4-Pro digital side buttons;
- native 1-bit SDMMC;
- CW2017 battery gauge;
- X4-Pro power/sleep topology.

## Current implementation

### Build target

`platformio.ini` now defaults to `x4pro` and builds for the ESP32-S3 N16R8 target.

```bash
pio run -e x4pro
```

The application image is:

```text
.pio/build/x4pro/firmware.bin
```

Generic upload is blocked deliberately:

```bash
pio run -e x4pro -t upload
```

must fail through `scripts/refuse_x4pro_upload.py`.

## FreeInk backend

The historical `open-x4-sdk` submodule path is retained to minimize changes in the Inx source tree, but it is pinned to the FreeInk SDK used for X4 Pro support.

The active X4 Pro board profile provides the hardware truth for:

- display SPI pins;
- runtime display-controller selection;
- GT911 touch;
- digital buttons;
- SDMMC and card-power control;
- battery gauge;
- peripheral power rails;
- deep-sleep wake configuration.

Inx code should not invent or duplicate X4 Pro GPIO mappings.

## Display

X4 Pro production batches may contain different compatible E-Ink controllers.

Before `display.begin()`, the port calls FreeInk's Xteink display-controller detection. FreeInk then chooses the matching driver while keeping the same application-facing display API.

The current Inx HAL maps unsupported legacy refresh modes conservatively onto FreeInk refresh modes. Advanced grayscale paths that depended on the old SDK are temporarily simplified during bring-up.

## Input compatibility layer

Inx 1.0.19 expects a button-oriented device. The X4 Pro has two discrete navigation buttons plus GT911 touch and a capacitive Home key.

Temporary mapping:

| Physical/Touch input | Inx logical action |
| --- | --- |
| first side key | Up / previous |
| second side key | Down / next |
| screen tap | Confirm |
| capacitive Home tap | Back |
| horizontal swipe | Left / Right |
| power button | Power |

This is intentionally a compatibility bridge, not the final touch UI.

## SD card

The X4 Pro uses native SDMMC rather than the legacy X4 SPI-SD path.

The build enables FreeInk's block-device interface:

```text
USE_BLOCK_DEVICE_INTERFACE=1
```

The existing Inx `SdMan` API can therefore remain largely unchanged while FreeInk performs X4-Pro-specific SDMMC initialization.

## Battery

Battery percentage is read through FreeInk `BatteryMonitor`, which uses the active X4 Pro profile and CW2017 gauge support.

Hardware validation of displayed percentage and charging-state behavior is still required on the first device.

## Sleep and wake

Legacy C3/X3 GPIO sleep code has been removed from the X4-Pro HAL path.

The port delegates rail shutdown and deep-sleep wake configuration to FreeInk `PowerManager`.

This code compiles, but actual sleep/wake behavior is part of the first-device validation checklist.

## OTA is disabled

The upstream Inx updater is not safe for an experimental X4-Pro port because it can install a generic `firmware.bin` and switch the OTA boot partition without validating the X4 Pro target.

Therefore the X4-Pro build excludes the updater implementation and defines:

```text
INX_DISABLE_OTA=1
```

Both online and SD-card firmware installation remain disabled until there is an X4-Pro-specific manifest and validation path.

## No-brick policy

Until first-flash and recovery are validated on real hardware:

1. never overwrite the bootloader;
2. never overwrite the live partition table;
3. never erase the whole flash;
4. never erase NVS during a normal test;
5. never blindly modify `otadata`;
6. never use generic PlatformIO upload;
7. never use upstream Inx OTA installation;
8. inspect the live device before writing;
9. create and checksum a full 16 MiB backup;
10. keep the known-good application slot intact;
11. write only the verified inactive OTA application slot during the first test;
12. verify the written bytes before changing boot selection.

## Read-only preflight

The repository includes:

```text
scripts/x4pro_inspect.py
```

Example:

```bash
python3 -m pip install --upgrade esptool
python3 scripts/x4pro_inspect.py \
  --port /dev/cu.usbmodemXXXX \
  --firmware .pio/build/x4pro/firmware.bin \
  --backup
```

The inspector contains no write/erase/boot-selection command. It:

- verifies ESP32-S3;
- reads the live partition table;
- parses partition offsets and sizes;
- requires multiple OTA application slots;
- verifies that the firmware fits;
- optionally reads all 16 MiB of flash;
- writes a SHA-256 digest for the backup.

A successful preflight is **not** automatically a flash approval. We must additionally identify the active slot and validate the recovery path.

## CI expectations

The port has two relevant GitHub Actions workflows:

### `CI`

Runs static analysis, formatting checks and the default firmware build on pull requests and `main`.

### `X4 Pro Build`

Runs the focused ESP32-S3 build for `x4pro-port` and uploads only application/debug artifacts:

```text
firmware.bin
firmware.elf
firmware.map
```

It must not publish `bootloader.bin` or `partitions.bin` as installable artifacts.

## First hardware test gate

No write should happen until all boxes below are satisfied:

```text
[ ] CI green
[ ] X4 Pro build green
[ ] connected MCU identifies as ESP32-S3
[ ] live partition table parsed successfully
[ ] at least two OTA app slots found
[ ] firmware fits the target slot
[ ] active slot identified
[ ] inactive test slot identified
[ ] complete 16 MiB flash backup created
[ ] backup SHA-256 recorded and verified
[ ] USB recovery access validated
[ ] known-good app slot remains untouched
```

## First boot test order

```text
[ ] boot / serial log
[ ] display-controller detection
[ ] E-Ink initialization
[ ] orientation
[ ] physical side buttons
[ ] touch tap
[ ] Home key
[ ] horizontal swipe
[ ] SDMMC mount
[ ] open an EPUB
[ ] battery reading
[ ] sleep
[ ] wake
[ ] restart
[ ] recovery to known-good slot
```

## Remaining port work

- validate the complete current HAL on physical X4 Pro hardware;
- integrate native touch hit-testing instead of compatibility button synthesis;
- integrate frontlight controls into the Inx UI;
- integrate RTC support;
- define and test a board-specific safe OTA format;
- automate inactive-slot detection and post-write verification only after the first manual recovery procedure is proven.
