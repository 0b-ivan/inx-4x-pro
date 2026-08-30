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

## Hardware backend

FreeInk is pinned as the `open-x4-sdk` submodule for a reproducible build. The application does not own X4 Pro GPIO numbers. Display, buttons/touch, battery, SDMMC and deep-sleep wake configuration come from FreeInk `BoardConfig`.

The X4 Pro may ship with different e-paper controllers. FreeInk controller detection is run before display initialization so the matching driver can be selected without changing Inx.

## Current limitations

- X4 Pro UI touch routing is not yet integrated into Inx navigation.
- Frontlight UI is not yet integrated.
- Inx RTC integration is not yet ported.
- OTA/update installation is intentionally disabled.
- Hardware flashing is intentionally disabled pending recovery validation.
