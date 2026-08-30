# Inx X4 Pro port

> **Experimental. Do not flash the current alpha builds to hardware yet.**

This fork ports Inx to the Xteink X4 Pro (ESP32-S3, 16 MB flash, 8 MB PSRAM) using the FreeInk X4 Pro board profile.

## No-brick policy

Until the first-flash procedure has been validated on the actual device, these rules are mandatory:

1. Never overwrite the bootloader.
2. Never overwrite the live partition table.
3. Never erase NVS or `otadata` as part of a normal test.
4. Never use generic `pio run -t upload`; the project intentionally blocks it.
5. Never install firmware through the upstream Inx OTA updater; OTA is compiled out in X4 Pro alpha builds.
6. Before the first hardware write, read and record the live partition table and active OTA slot.
7. Take a full 16 MB flash backup and record its SHA-256.
8. Write only `firmware.bin` to the validated inactive OTA **application** slot.
9. Keep the currently working application slot intact until the new application has completed boot, display, input, SD and sleep tests.
10. Do not publish `bootloader.bin` or `partitions.bin` as installable release assets.

## Build

```bash
pio run -e x4pro
```

The useful test artifact is:

```text
.pio/build/x4pro/firmware.bin
```

Building a binary is not permission to flash it. The hardware-test gate remains the validated inactive-slot procedure above.

## Read-only device preflight

Install `esptool`, connect the X4 Pro and run the inspector before any hardware write:

```bash
python3 -m pip install --upgrade esptool
python3 scripts/x4pro_inspect.py \
  --port /dev/cu.usbmodemXXXX \
  --firmware .pio/build/x4pro/firmware.bin \
  --backup
```

The inspector has no write/erase command. It verifies that the connected chip reports as ESP32-S3, reads and parses the live partition table, requires at least two OTA application slots, checks that `firmware.bin` fits those slots, and optionally saves the complete 16 MiB flash plus its SHA-256 digest.

A successful read-only preflight still does **not** flash or select the experimental application. Active/inactive OTA-slot selection is a separate safety gate.

## Hardware backend

FreeInk is pinned as the `open-x4-sdk` submodule for a reproducible build. The application does not own X4 Pro GPIO numbers. Display, buttons/touch, battery, SDMMC and deep-sleep wake configuration come from FreeInk `BoardConfig`.

The X4 Pro may ship with different e-paper controllers. FreeInk controller detection is run before display initialization so the matching driver can be selected without changing Inx.

### Temporary Inx input compatibility

Inx 1.0.19 is button-oriented while the X4 Pro has two discrete navigation buttons plus GT911 touch. Until Inx gets native touch hit-testing, the HAL exposes the X4 Pro input as:

- physical left side key -> `Up` / previous
- physical right side key -> `Down` / next
- screen tap -> `Confirm`
- capacitive Home tap -> `Back`
- horizontal swipe left/right -> logical `Right` / `Left`
- power button -> `Power`

This preserves the existing Inx navigation model without inventing new GPIO mappings.

## Current limitations

- Touch uses the compatibility mapping above; native per-widget hit-testing is not integrated yet.
- Frontlight UI is not yet integrated.
- Inx RTC integration is not yet ported.
- OTA/update installation is intentionally disabled.
- Hardware flashing is intentionally disabled pending recovery validation.
