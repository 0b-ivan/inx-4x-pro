#!/usr/bin/env python3
"""Fail-closed Xteink X4 Pro OTA boot-slot switch helper.

Default mode is DRY RUN. With --write, this helper may modify ONLY the first
4 KiB sector of the 8 KiB otadata partition at 0xE000. The second otadata
sector at 0xF000 is deliberately preserved as the original app1 rescue record.

Supported, intentionally narrow state transitions:
  stock/test preparation: app1 (factory seq1/seq2) -> app0 (write seq3 to sector0)
  manual rollback:        app0 (seq3/seq2) -> app1 (restore factory sector0 from backup)

Rollback restores the exact original first otadata sector instead of inventing a
new sequence number. This returns the device to the same factory seq1/seq2 state
captured in the verified full-flash backup, so repeated app1 -> stage app0 -> test
cycles remain fail-closed and reproducible.

No bootloader, partition table, NVS, app image, SPIFFS, coredump, or second
otadata sector is ever written by this script. After a write it verifies the
entire protected metadata region and leaves the device in the ROM bootloader;
the selected app starts only after a deliberate power-cycle/reset.
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
    OTA_RECORD_SIZE,
    OTADATA_OFFSET,
    OTADATA_SIZE,
    check_firmware_image,
    check_security,
    determine_active_slot,
    validate_factory_layout,
)

PROTECTED_META_OFFSET = PARTITION_TABLE_OFFSET
PROTECTED_META_SIZE = 0x00008000  # 0x8000..0xffff: table + NVS + otadata
OTADATA_SECTOR0_REL = OTADATA_OFFSET - PROTECTED_META_OFFSET
OTADATA_SECTOR1_REL = OTADATA_SECTOR0_REL + FLASH_SECTOR_SIZE


def crc_for_seq(seq: int) -> int:
    return zlib.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF


def build_valid_otadata_sector(seq: int) -> bytes:
    sector = bytearray(b"\xff" * FLASH_SECTOR_SIZE)
    struct.pack_into("<I", sector, 0, seq)
    # bytes 4..23 are seq_label and remain erased (0xff), matching factory data.
    struct.pack_into("<I", sector, 24, OTA_IMG_VALID)
    struct.pack_into("<I", sector, 28, crc_for_seq(seq))
    return bytes(sector)


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


def esptool_write_sector_no_boot(port: str, offset: int, source: Path) -> None:
    if source.stat().st_size != FLASH_SECTOR_SIZE:
        raise RuntimeError("Refusing to write anything other than exactly one 4 KiB sector.")
    if offset != OTADATA_OFFSET:
        raise RuntimeError(f"Refusing write outside first otadata sector: 0x{offset:x}.")
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
        hex(offset),
        str(source),
    ]
    subprocess.run(cmd, check=True)


def backup_bytes(backup: Path) -> bytes:
    if not backup.is_file():
        raise RuntimeError(f"Backup not found: {backup}")
    if backup.stat().st_size != FLASH_SIZE:
        raise RuntimeError(
            f"Backup must be exactly {FLASH_SIZE} bytes; got {backup.stat().st_size}."
        )
    digest = sha256(backup)
    digest_file = backup.with_suffix(backup.suffix + ".sha256")
    if digest_file.exists():
        recorded = digest_file.read_text(encoding="utf-8").strip().split()
        if not recorded or recorded[0].lower() != digest.lower():
            raise RuntimeError("Backup SHA-256 sidecar does not match the full-flash backup.")
    print(f"Backup verified: {backup} SHA-256={digest}")
    return backup.read_bytes()


def verify_against_backup(backup: bytes, live_meta: bytes, target: str) -> None:
    original = backup[
        PROTECTED_META_OFFSET : PROTECTED_META_OFFSET + PROTECTED_META_SIZE
    ]
    if target == "app0":
        if live_meta != original:
            raise RuntimeError(
                "Current partition/NVS/otadata metadata no longer exactly matches the original backup."
            )
        return

    # Rollback state is allowed to differ from the factory backup ONLY in
    # otadata sector0. Everything else, especially sector1 rescue data, must
    # still match the original full-flash backup byte-for-byte.
    if live_meta[:OTADATA_SECTOR0_REL] != original[:OTADATA_SECTOR0_REL]:
        raise RuntimeError("Protected metadata before otadata differs from the original backup.")
    if live_meta[OTADATA_SECTOR1_REL:] != original[OTADATA_SECTOR1_REL:]:
        raise RuntimeError("otadata sector1 or adjacent metadata differs from the original backup.")


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
        help="Required for app0 selection; exact staged firmware.bin is re-read and SHA-verified",
    )
    parser.add_argument("--write", action="store_true")
    parser.add_argument(
        "--out",
        type=Path,
        default=Path("x4pro-preflight"),
        help="Directory for persistent before/after otadata snapshots",
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
            read_flash(args.port, PROTECTED_META_OFFSET, PROTECTED_META_SIZE, live_meta_file)
            live_meta = live_meta_file.read_bytes()

            partition_raw = live_meta[
                PARTITION_TABLE_OFFSET - PROTECTED_META_OFFSET :
                PARTITION_TABLE_OFFSET - PROTECTED_META_OFFSET + PARTITION_TABLE_SIZE
            ]
            entries = parse_partitions(partition_raw)
            ota_apps = validate_partitions(entries, None)
            app0, app1 = validate_factory_layout(entries)

            otadata = live_meta[
                OTADATA_SECTOR0_REL : OTADATA_SECTOR0_REL + OTADATA_SIZE
            ]
            active, inactive, records = determine_active_slot(otadata, ota_apps)
            record_summary(records)

            original_backup = backup_bytes(args.backup)
            verify_against_backup(original_backup, live_meta, args.target)
            original_meta = original_backup[
                PROTECTED_META_OFFSET : PROTECTED_META_OFFSET + PROTECTED_META_SIZE
            ]
            factory_otadata = original_meta[
                OTADATA_SECTOR0_REL : OTADATA_SECTOR0_REL + OTADATA_SIZE
            ]
            factory_active, _, factory_records = determine_active_slot(factory_otadata, ota_apps)
            factory_sector0, factory_sector1 = factory_records
            if not (
                factory_active.label == "app1"
                and factory_sector0.valid
                and factory_sector0.seq == 1
                and factory_sector1.valid
                and factory_sector1.seq == 2
            ):
                raise RuntimeError(
                    "Full-flash backup does not contain the expected factory app1 seq1/seq2 rescue state."
                )

            sector0, sector1 = records
            if args.target == "app0":
                if active.label != "app1" or inactive.label != "app0":
                    raise RuntimeError(
                        f"First test requires active=app1/inactive=app0; got {active.label}/{inactive.label}."
                    )
                if not (
                    sector0.valid
                    and sector0.seq == 1
                    and sector1.valid
                    and sector1.seq == 2
                ):
                    raise RuntimeError("Expected untouched factory otadata seq1/seq2 before app0 test.")
                if args.firmware is None or not args.firmware.is_file():
                    raise RuntimeError("--firmware is required when selecting app0.")
                check_firmware_image(args.firmware)
                if args.firmware.stat().st_size > app0.size:
                    raise RuntimeError("Firmware does not fit app0.")

                staged_readback = temp / "app0-before-boot.bin"
                print("\nRe-reading staged app0 before changing boot selection...")
                read_flash(args.port, app0.offset, args.firmware.stat().st_size, staged_readback)
                firmware_hash = sha256(args.firmware)
                staged_hash = sha256(staged_readback)
                print(f"  firmware SHA-256: {firmware_hash}")
                print(f"  staged   SHA-256: {staged_hash}")
                if staged_hash != firmware_hash:
                    raise RuntimeError("app0 no longer matches the supplied firmware image.")

                candidate_sector0 = build_valid_otadata_sector(3)
                candidate_seq = 3
                candidate_crc = crc_for_seq(candidate_seq)
                candidate_description = "new test selector"
                expected_active = app0
            else:
                if active.label != "app0":
                    raise RuntimeError(f"Rollback requires active=app0; currently active={active.label}.")
                if not (
                    sector0.valid
                    and sector0.seq == 3
                    and sector1.valid
                    and sector1.seq == 2
                ):
                    raise RuntimeError(
                        "Rollback requires verified seq3 in sector0 and untouched seq2 rescue record in sector1."
                    )

                # Restore the exact 4 KiB sector captured before any test. This
                # deliberately returns otadata to seq1/seq2 rather than producing
                # seq4, so the next staging run can demand an exact backup match.
                candidate_sector0 = original_meta[
                    OTADATA_SECTOR0_REL : OTADATA_SECTOR1_REL
                ]
                candidate_seq = factory_sector0.seq
                candidate_crc = factory_sector0.crc
                candidate_description = "exact factory sector restored from backup"
                expected_active = app1

            args.out.mkdir(parents=True, exist_ok=True)
            before_snapshot = args.out / f"otadata-before-{args.target}.bin"
            before_snapshot.write_bytes(otadata)

            print("\nValidated boot-selection plan:")
            print(f"  Current active: app{1 if active.label == 'app1' else 0}")
            print(f"  Requested:      {args.target}")
            print(f"  Write address:  0x{OTADATA_OFFSET:x}")
            print(f"  Write size:     0x{FLASH_SECTOR_SIZE:x} (one sector only)")
            print(
                f"  New sector0:    seq={candidate_seq}, state={OTA_IMG_VALID}, "
                f"crc=0x{candidate_crc:08x} ({candidate_description})"
            )
            print("  sector1 @ 0xf000: PRESERVED / NOT WRITTEN")
            print("  Bootloader:         NOT WRITTEN")
            print("  Partition table:    NOT WRITTEN")
            print("  NVS:                NOT WRITTEN")
            print("  app0/app1 contents: NOT WRITTEN")
            print("  Device after write: LEFT IN BOOTLOADER; explicit power-cycle required")

            if not args.write:
                print("\nDRY RUN OK")
                print("No flash data was modified.")
                return 0

            sector_file = temp / "otadata-sector0-new.bin"
            sector_file.write_bytes(candidate_sector0)
            original_sector1 = otadata[FLASH_SECTOR_SIZE:OTADATA_SIZE]

            print("\nWRITING FIRST OTADATA SECTOR ONLY...")
            esptool_write_sector_no_boot(args.port, OTADATA_OFFSET, sector_file)

            after_file = temp / "protected-meta-after.bin"
            print("\nReading protected metadata back without booting the selected app...")
            esptool_read_no_boot(
                args.port,
                PROTECTED_META_OFFSET,
                PROTECTED_META_SIZE,
                after_file,
            )
            after = after_file.read_bytes()

            if after[:OTADATA_SECTOR0_REL] != live_meta[:OTADATA_SECTOR0_REL]:
                raise RuntimeError("Partition table/NVS changed unexpectedly during boot-selector write.")
            if after[OTADATA_SECTOR0_REL:OTADATA_SECTOR1_REL] != candidate_sector0:
                raise RuntimeError("otadata sector0 readback does not exactly match the intended record.")
            if after[OTADATA_SECTOR1_REL:] != live_meta[OTADATA_SECTOR1_REL:]:
                raise RuntimeError("otadata sector1 or adjacent metadata changed unexpectedly.")
            if after[
                OTADATA_SECTOR1_REL : OTADATA_SECTOR1_REL + FLASH_SECTOR_SIZE
            ] != original_sector1:
                raise RuntimeError("The app1 rescue otadata sector was not preserved byte-for-byte.")

            after_otadata = after[
                OTADATA_SECTOR0_REL : OTADATA_SECTOR0_REL + OTADATA_SIZE
            ]
            new_active, _, new_records = determine_active_slot(after_otadata, ota_apps)
            record_summary(new_records)
            if new_active.label != expected_active.label:
                raise RuntimeError(
                    f"Readback selects {new_active.label}, expected {expected_active.label}."
                )

            if args.target == "app1" and after != original_meta:
                raise RuntimeError(
                    "Rollback selected app1 but protected metadata was not restored exactly to the original backup."
                )

            after_snapshot = args.out / f"otadata-after-{args.target}.bin"
            after_snapshot.write_bytes(after_otadata)

            print("\nBOOT SELECTION VERIFIED")
            print(f"Selected slot: {new_active.label}")
            print("Original app1 rescue record in sector1 is intact.")
            if args.target == "app1":
                print("Factory protected metadata is restored exactly to the verified full-flash backup.")
            print("Device is intentionally left in the bootloader.")
            print("Power-cycle WITHOUT holding the left button to boot the selected slot.")
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
            "Do not erase flash or use generic PlatformIO upload. Use the proven GPIO0 recovery path if needed.",
            file=sys.stderr,
        )
        return 2


if __name__ == "__main__":
    raise SystemExit(main())