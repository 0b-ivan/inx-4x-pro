# Inx X4 Pro port — technical status

> [!WARNING]
> **Experimental. Hardware testing is allowed only through the guarded inactive-slot workflow documented in this repository. Generic flashing remains unsafe.**

For a beginner-friendly explanation of bootloaders, partitions, OTA slots, backups and the planned first hardware test, read [`X4PRO_FLASHING_GUIDE.md`](X4PRO_FLASHING_GUIDE.md).

## Goal

Port Inx 1.0.19 from the original Xteink hardware target to the **Xteink X4 Pro** while keeping hardware tests reversible.

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

Current temporary mapping:

| Physical/Touch input | Inx logical action |
| --- | --- |
| left side key | Up / previous |
| right side key | Down / next |
| bottom-left touch zone | Back / Menu |
| bottom-right touch zone | Confirm / Open |
| left touch edge | Left |
| right touch edge | Right |
| upper touch zone | Up |
| lower touch zone | Down |
| center touch zone | Confirm |
| horizontal swipe | Left / Right content navigation |
| vertical swipe | Up / Down list navigation |
| capacitive Home tap | Back / Menu |
| power button | Power |

This is intentionally a compatibility bridge, not the final touch UI.

### Touch orientation — physically confirmed

FreeInk exposes GT911 positions normalized in the panel-native landscape frame. Inx renders its default UI in logical portrait orientation. The compatibility bridge therefore uses:

```text
portraitX = 1 - panelY
portraitY = panelX
```

A four-corner test on the physical X4 Pro confirmed that this transform is correct with the currently pinned FreeInk profile:

| Physical corner | Panel-native log | Inx portrait result |
| --- | --- | --- |
| top-left | `(0.023, 0.956)` | `(0.044, 0.023)` |
| top-right | `(0.033, 0.023)` | `(0.977, 0.033)` |
| bottom-left | `(0.972, 0.916)` | `(0.084, 0.972)` |
| bottom-right | `(0.972, 0.061)` | `(0.939, 0.972)` |

The tested unit therefore requires **no additional app-layer X/Y flip** beyond the existing axis swap/orientation transform. If the FreeInk board profile changes later, repeat the corner test before changing this mapping.

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

Basic sleep/wake behavior has been observed on the test device: USB-Serial/JTAG disappears while sleeping and re-enumerates after a physical power-button wake. More deliberate sleep-duration and wake-source testing is still required.

## OTA is disabled

The upstream Inx updater is not safe for an experimental X4-Pro port because it can install a generic `firmware.bin` and switch the OTA boot partition without validating the X4 Pro target.

Therefore the X4-Pro build excludes the updater implementation and defines:

```text
INX_DISABLE_OTA=1
```

Both online and SD-card firmware installation remain disabled until there is an X4-Pro-specific manifest and validation path.

## No-brick policy

1. never overwrite the bootloader during normal testing;
2. never overwrite the live partition table;
3. never erase the whole flash;
4. never erase NVS during a normal test;
5. modify `otadata` only through the guarded boot-slot helper;
6. never use generic PlatformIO upload;
7. never use upstream Inx OTA installation;
8. inspect the live device before writing;
9. create and checksum a full 16 MiB backup;
10. keep the known-good application slot intact;
11. write only the verified inactive OTA application slot;
12. verify written bytes before changing boot selection;
13. restore the factory otadata sector from the verified backup when rolling back to app1.

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

A successful preflight is **not** automatically a flash approval. The guarded stage and boot-slot scripts additionally validate active/inactive slots, security state, backup consistency and post-write readback.

## CI expectations

The port has two relevant GitHub Actions workflows:

### `CI`

Runs static analysis, formatting checks and the default firmware build on pull requests and `main`.

### `X4 Pro Build`

Runs the focused ESP32-S3 build for `main` and uploads the application image.

Installable release assets must not publish `bootloader.bin` or `partitions.bin` for the X4 Pro.

## Hardware-test gate

Confirmed on the current test device:

```text
[x] connected MCU identifies as ESP32-S3
[x] live partition table parsed successfully
[x] two OTA app slots found
[x] firmware fits the inactive target slot
[x] active/inactive slots identified
[x] complete 16 MiB flash backup created and SHA-256 verified
[x] Secure Boot disabled
[x] Flash Encryption disabled
[x] manual GPIO0 ROM recovery access validated
[x] original app1 kept untouched as rescue slot
[x] staged app0 image read back and SHA-256 verified
[x] guarded app1 -> app0 selection validated
[x] guarded app0 -> factory app1 rollback validated
[x] test firmware boots and renders on the physical panel
[x] GT911 corner calibration validated
```

## Hardware validation order

```text
[x] boot / serial log
[x] display-controller detection
[x] E-Ink initialization
[x] portrait orientation
[x] physical side buttons
[x] touch tap
[x] Home key
[x] touch corner orientation
[ ] horizontal swipe behavior
[ ] vertical swipe behavior
[ ] complete menu navigation by touch
[ ] SDMMC mount/content behavior
[ ] open an EPUB
[ ] battery reading accuracy
[ ] deliberate sleep/wake cycle
[ ] restart
[x] recovery path to known-good slot
```

## Remaining port work

- replace compatibility button synthesis with native touch hit-testing where useful;
- validate four-direction swipe behavior on physical hardware;
- investigate missing `/.metadata/books.bin` and `.system/statistics.bin` files during empty-storage startup;
- validate SDMMC mount and book discovery;
- integrate frontlight controls into the Inx UI;
- integrate RTC support;
- validate battery/charging telemetry;
- harden sleep/wake behavior;
- define and test a board-specific safe OTA format.
