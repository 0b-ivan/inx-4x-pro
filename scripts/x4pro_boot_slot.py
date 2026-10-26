#!/usr/bin/env python3
"""Fail-closed Xteink X4 Pro OTA boot-slot selector.

This helper is intentionally tailored to the verified X4 Pro layout:

  app0 @ 0x00010000, size 0x007f0000  -> rescue/original firmware
  app1 @ 0x00800000, size 0x007f0000  -> staged test firmware
  otadata @ 0x0000e000, size 0x2000

The original app0 selector in otadata sector0 (0xe000) is never written.
Selecting app1 writes only otadata sector1 (0xf000) with seq=2.
Rolling back to app0 restores only that second sector from the verified
full-flash backup.

Default mode is DRY RUN. Nothing is written unless --write is supplied.
"""

from __future__ import annotations

import argparse
import struct
import subprocess
import sys
import tempfile
import zlib
from pathlib import Path

from x4pro_inspect import (
    FLASH_SIZE,
    PARTITION_TABLE_OFFSET,
    PARTITION_TABLE_SIZE,
    identify_chip,
    parse_partitions,
    read_flash,
    sha256,
    validate_partitions,
)
from x4pro_stage_inactive import (
    FLASH_SECTOR_SIZE,
    OTA_IMG_VALID,
    OTADATA_OFFSET,
    OTADATA_SIZE,
    check_firmware_image,
    check_security,
    determine_active_slot,
    validate_factory_layout,
)

PROTECTED_META_OFFSET = PARTITION_TABLE_OFFSET
PROTECTED_META_SIZE = 0x00008000
OTADATA_SECTOR0_REL = OTADATA_OFFSET - PROTECTED_META_OFFSET
OTADATA_SECTOR1_REL = OTADATA_SECTOR0_REL + FLASH_SECTOR_SIZE
OTADATA_SECTOR1_OFFSET = OTADATA_OFFSET + FLASH_SECTOR_SIZE


def crc_for_seq(seq: int) -> int:
    return zlib.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF


def build_valid_otadata_sector(seq: int) -> bytes:
    sector = bytearray(b"\xff" * FLASH_SECTOR_SIZE)
    struct.pack_into("<I", sector, 0, seq)
    struct.pack_into("<I", sector, 24, OTA_IMG_VALID)
    struct.pack_into("<I", sector, 28, crc_for_seq(seq))
    return bytes(sector)


def erased_record(record) -> bool:
    return (
        not record.valid
        and record.seq == 0xFFFFFFFF
        and record.state == 0xFFFFFFFF
        and record.crc == 0xFFFFFFFF
    )


def esptool_read_no_boot(port: str, offset: int, size: int, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    cmd = [
        sys.executable,
        "-m",
        "esptool",
        "--port",
        port,
        "--no-stub",
        "--after",
        "no-reset",
        "read-flash",
        hex(offset),
        hex(size),
        str(destination),
    ]
    subprocess.run(cmd, check=True)
    if destination.stat().st_size != size:
        raise RuntimeError(
            f"Short read: expected {size} bytes, got {destination.stat().st_size}."
        )


def esptool_write_sector1_no_boot(port: str, source: Path) -> None:
    if source.stat().st_size != FLASH_SECTOR_SIZE:
        raise RuntimeError(
            "Refusing to write anything other than exactly one 4 KiB sector."
        )

    cmd = [
        sys.executable,
        "-m",
        "esptool",
        "--port",
        port,
        "--no-stub",
        "--after",
        "no-reset",
        "write-flash",
        hex(OTADATA_SECTOR1_OFFSET),
        str(source),
    ]
    subprocess.run(cmd, check=True)


def load_backup(path: Path) -> bytes:
    if not path.is_file():
        raise RuntimeError(f"Backup not found: {path}")
    if path.stat().st_size != FLASH_SIZE:
        raise RuntimeError(
            f"Backup must be exactly {FLASH_SIZE} bytes; got {path.stat().st_size}."
        )

    digest = sha256(path)
    sidecar = path.with_suffix(path.suffix + ".sha256")
    if sidecar.exists():
        recorded = sidecar.read_text(encoding="utf-8").strip().split()
        if not recorded or recorded[0].lower() != digest.lower():
            raise RuntimeError(
                "Backup SHA-256 sidecar does not match the full-flash backup."
            )

    print(f"Backup verified: {path} SHA-256={digest}")
    return path.read_bytes()


def record_summary(records) -> None:
    print("\notadata:")
    for record in records:
        status = "VALID" if record.valid else "IGNORED"
        print(
            f"  sector{record.sector}: seq={record.seq} state={record.state} "
            f"crc=0x{record.crc:08x} ({status})"
        )


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Guarded X4 Pro app0/app1 boot selector; dry-run unless --write is supplied"
    )
    parser.add_argument("--port", required=True)
    parser.add_argument("--backup", type=Path, required=True)
    parser.add_argument("--target", choices=("app0", "app1"), required=True)
    parser.add_argument(
        "--firmware",
        type=Path,
        help="Required for app1 selection; staged app1 is re-read and SHA-verified",
    )
    parser.add_argument("--write", action="store_true")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("x4pro-preflight"),
        help="Directory for before/after otadata snapshots",
    )
    args = parser.parse_args()

    try:
        print("X4 Pro guarded boot-slot preflight")
        print("==================================")
        identify_chip(args.port)
        check_security(args.port)

        with tempfile.TemporaryDirectory(prefix="x4pro-boot-slot-") as tmp:
            temp = Path(tmp)

            live_meta_file = temp / "protected-meta-before.bin"
            read_flash(
                args.port,
                PROTECTED_META_OFFSET,
                PROTECTED_META_SIZE,
                live_meta_file,
            )
            live_meta = live_meta_file.read_bytes()

            partition_raw = live_meta[
                PARTITION_TABLE_OFFSET - PROTECTED_META_OFFSET :
                PARTITION_TABLE_OFFSET - PROTECTED_META_OFFSET + PARTITION_TABLE_SIZE
            ]
            entries = parse_partitions(partition_raw)
            ota_apps = validate_partitions(entries, None)
            app0, app1 = validate_factory_layout(entries)

            backup = load_backup(args.backup)
            backup_meta = backup[
                PROTECTED_META_OFFSET :
                PROTECTED_META_OFFSET + PROTECTED_META_SIZE
            ]

            if live_meta[:PARTITION_TABLE_SIZE] != backup_meta[:PARTITION_TABLE_SIZE]:
                raise RuntimeError(
                    "Current partition table differs from the verified backup."
                )

            backup_otadata = backup_meta[
                OTADATA_SECTOR0_REL :
                OTADATA_SECTOR0_REL + OTADATA_SIZE
            ]
            backup_active, _, backup_records = determine_active_slot(
                backup_otadata, ota_apps
            )
            backup_sector0, backup_sector1 = backup_records

            if not (
                backup_active.label == "app0"
                and backup_sector0.valid
                and backup_sector0.seq == 1
                and erased_record(backup_sector1)
            ):
                raise RuntimeError(
                    "Backup is not the verified stock app0 state "
                    "(sector0 seq1 valid, sector1 erased)."
                )

            if (
                live_meta[OTADATA_SECTOR0_REL:OTADATA_SECTOR1_REL]
                != backup_meta[OTADATA_SECTOR0_REL:OTADATA_SECTOR1_REL]
            ):
                raise RuntimeError(
                    "app0 rescue selector in otadata sector0 differs from backup."
                )

            otadata = live_meta[
                OTADATA_SECTOR0_REL :
                OTADATA_SECTOR0_REL + OTADATA_SIZE
            ]
            active, inactive, records = determine_active_slot(otadata, ota_apps)
            sector0, sector1 = records
            record_summary(records)

            if args.target == "app1":
                if active.label != "app0" or inactive.label != "app1":
                    raise RuntimeError(
                        f"app1 test requires active=app0/inactive=app1; "
                        f"got {active.label}/{inactive.label}."
                    )
                if not (
                    sector0.valid
                    and sector0.seq == 1
                    and erased_record(sector1)
                ):
                    raise RuntimeError(
                        "Expected app0 rescue state: sector0 seq1 valid, sector1 erased."
                    )
                if args.firmware is None or not args.firmware.is_file():
                    raise RuntimeError(
                        "--firmware is required when selecting app1."
                    )

                check_firmware_image(args.firmware)
                if args.firmware.stat().st_size > app1.size:
                    raise RuntimeError("Firmware does not fit app1.")

                staged = temp / "app1-before-boot.bin"
                print("\nRe-reading staged app1 before changing boot selection...")
                read_flash(
                    args.port,
                    app1.offset,
                    args.firmware.stat().st_size,
                    staged,
                )
                firmware_hash = sha256(args.firmware)
                staged_hash = sha256(staged)
                print(f"  firmware SHA-256: {firmware_hash}")
                print(f"  staged   SHA-256: {staged_hash}")
                if staged_hash != firmware_hash:
                    raise RuntimeError(
                        "app1 no longer matches the supplied firmware image."
                    )

                candidate_sector1 = build_valid_otadata_sector(2)
                candidate_seq = 2
                candidate_crc = crc_for_seq(candidate_seq)
                candidate_description = "app1 test selector"
                expected_active = app1

            else:
                if active.label != "app1":
                    raise RuntimeError(
                        f"Rollback requires active=app1; currently active={active.label}."
                    )
                if not (
                    sector0.valid
                    and sector0.seq == 1
                    and sector1.valid
                    and sector1.seq == 2
                ):
                    raise RuntimeError(
                        "Rollback requires sector0 seq1 and sector1 seq2."
                    )

                candidate_sector1 = backup_meta[
                    OTADATA_SECTOR1_REL :
                    OTADATA_SECTOR1_REL + FLASH_SECTOR_SIZE
                ]
                candidate_seq = 0xFFFFFFFF
                candidate_crc = 0xFFFFFFFF
                candidate_description = "exact erased sector restored from backup"
                expected_active = app0

            args.out.mkdir(parents=True, exist_ok=True)
            (args.out / f"otadata-before-{args.target}.bin").write_bytes(otadata)

            print("\nValidated boot-selection plan:")
            print(f"  Current active: {active.label}")
            print(f"  Requested:      {args.target}")
            print(f"  Write address:  0x{OTADATA_SECTOR1_OFFSET:x}")
            print(f"  Write size:     0x{FLASH_SECTOR_SIZE:x} (one sector only)")
            print(
                f"  New sector1:    seq={candidate_seq}, "
                f"crc=0x{candidate_crc:08x} ({candidate_description})"
            )
            print("  sector0 @ 0xe000: PRESERVED / NOT WRITTEN (app0 rescue)")
            print("  Bootloader:         NOT WRITTEN")
            print("  Partition table:    NOT WRITTEN")
            print("  NVS:                NOT WRITTEN")
            print("  app0/app1 contents: NOT WRITTEN")
            print(
                "  Device after write: LEFT IN BOOTLOADER; "
                "explicit power-cycle required"
            )

            if not args.write:
                print("\nDRY RUN OK")
                print("No flash data was modified.")
                return 0

            sector_file = temp / "otadata-sector1-new.bin"
            sector_file.write_bytes(candidate_sector1)

            print("\nWRITING SECOND OTADATA SECTOR ONLY...")
            esptool_write_sector1_no_boot(args.port, sector_file)

            after_file = temp / "protected-meta-after.bin"
            print(
                "\nReading protected metadata back without booting "
                "the selected app..."
            )
            esptool_read_no_boot(
                args.port,
                PROTECTED_META_OFFSET,
                PROTECTED_META_SIZE,
                after_file,
            )
            after = after_file.read_bytes()

            if after[:OTADATA_SECTOR1_REL] != live_meta[:OTADATA_SECTOR1_REL]:
                raise RuntimeError(
                    "Partition table, NVS or app0 rescue selector changed unexpectedly."
                )

            if (
                after[
                    OTADATA_SECTOR1_REL :
                    OTADATA_SECTOR1_REL + FLASH_SECTOR_SIZE
                ]
                != candidate_sector1
            ):
                raise RuntimeError(
                    "otadata sector1 readback does not match the intended record."
                )

            after_otadata = after[
                OTADATA_SECTOR0_REL :
                OTADATA_SECTOR0_REL + OTADATA_SIZE
            ]
            new_active, _, new_records = determine_active_slot(
                after_otadata, ota_apps
            )
            record_summary(new_records)

            if new_active.label != expected_active.label:
                raise RuntimeError(
                    f"Readback selects {new_active.label}, "
                    f"expected {expected_active.label}."
                )

            if args.target == "app0" and after_otadata != backup_otadata:
                raise RuntimeError(
                    "Rollback selected app0 but original otadata was not restored exactly."
                )

            (args.out / f"otadata-after-{args.target}.bin").write_bytes(
                after_otadata
            )

            print("\nBOOT SELECTION VERIFIED")
            print(f"Selected slot: {new_active.label}")
            print("app0 rescue selector in sector0 is intact.")
            if args.target == "app0":
                print("Original otadata was restored exactly.")
            print("Device is intentionally left in the bootloader.")
            print(
                "Power-cycle WITHOUT holding the left/BOOT button "
                "to boot the selected slot."
            )
            return 0

    except (
        OSError,
        RuntimeError,
        subprocess.CalledProcessError,
        subprocess.SubprocessError,
        ValueError,
    ) as exc:
        print(f"\nREFUSED / FAILED: {exc}", file=sys.stderr)
        print(
            "Do not erase flash or use generic PlatformIO upload. "
            "Use the proven GPIO0 recovery path if needed.",
            file=sys.stderr,
        )
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
