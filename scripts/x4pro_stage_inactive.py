#!/usr/bin/env python3
"""Fail-closed Xteink X4 Pro inactive-slot staging helper.

Default mode is DRY RUN. It performs only reads and validation.

With --write it may write exactly one application image to the currently
inactive OTA application slot. It never writes bootloader, partition table,
NVS, otadata, SPIFFS, coredump, or the active OTA application slot.

Before any write it requires:
  * ESP32-S3 identification,
  * Secure Boot disabled,
  * Flash Encryption disabled,
  * the known X4 Pro factory 2x0x7f0000 OTA layout,
  * factory app0 selector state (valid seq1 in sector0, erased sector1),
  * a full 16 MiB backup whose partition table and app0 rescue record match,
  * an ESP32-S3 application image that fits the inactive slot.

NVS is intentionally allowed to drift from the old full-flash backup because
legitimate firmware boots may update runtime NVS. Staging still snapshots the
entire protected metadata region before writing and requires it to remain
byte-for-byte unchanged afterwards.

After staging, it reads the staged image back and compares SHA-256, then
re-reads the protected metadata region and verifies it is byte-for-byte
unchanged. Boot selection is intentionally NOT changed.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
import sys
import tempfile
import zlib
from dataclasses import dataclass
from pathlib import Path

from x4pro_inspect import (
    FLASH_SIZE,
    PARTITION_TABLE_OFFSET,
    PARTITION_TABLE_SIZE,
    Partition,
    identify_chip,
    parse_partitions,
    read_flash,
    sha256,
    validate_partitions,
)

EXPECTED_APP0 = (0x00010000, 0x007F0000)
EXPECTED_APP1 = (0x00800000, 0x007F0000)
OTADATA_OFFSET = 0x0000E000
OTADATA_SIZE = 0x00002000
PROTECTED_META_OFFSET = PARTITION_TABLE_OFFSET
PROTECTED_META_SIZE = 0x00008000  # partition table + NVS + otadata, up to app0
OTA_RECORD_SIZE = 32
FLASH_SECTOR_SIZE = 0x1000
OTA_IMG_VALID = 2


@dataclass(frozen=True)
class OtaRecord:
    sector: int
    seq: int
    state: int
    crc: int
    expected_crc: int

    @property
    def valid(self) -> bool:
        return (
            self.seq not in (0, 0xFFFFFFFF)
            and self.state == OTA_IMG_VALID
            and self.crc == self.expected_crc
        )


def run_capture(cmd: list[str]) -> str:
    result = subprocess.run(cmd, text=True, capture_output=True)
    text = f"{result.stdout}\n{result.stderr}".strip()
    if result.returncode != 0:
        if text:
            print(text, file=sys.stderr)
        raise RuntimeError(f"Command failed: {' '.join(cmd)}")
    return text


def check_security(port: str) -> None:
    text = run_capture(
        [
            sys.executable,
            "-m",
            "esptool",
            "--port",
            port,
            "--no-stub",
            "get-security-info",
        ]
    )
    print("\nSecurity check:")
    for line in text.splitlines():
        if "Secure Boot:" in line or "Flash Encryption:" in line or "Flags:" in line:
            print(f"  {line.strip()}")

    if "Secure Boot: Disabled" not in text:
        raise RuntimeError("Secure Boot is not confirmed disabled; refusing to stage.")
    if "Flash Encryption: Disabled" not in text:
        raise RuntimeError("Flash Encryption is not confirmed disabled; refusing to stage.")


def check_firmware_image(firmware: Path) -> None:
    text = run_capture([sys.executable, "-m", "esptool", "image-info", str(firmware)])
    if "ESP32-S3" not in text and "Chip ID: 9" not in text:
        raise RuntimeError("Firmware image was not identified as an ESP32-S3 image.")
    print("Firmware image target: ESP32-S3")


def parse_ota_record(raw: bytes, sector: int) -> OtaRecord:
    if len(raw) < OTA_RECORD_SIZE:
        raise RuntimeError("Short otadata record.")
    seq = struct.unpack_from("<I", raw, 0)[0]
    state = struct.unpack_from("<I", raw, 24)[0]
    crc = struct.unpack_from("<I", raw, 28)[0]
    expected_crc = zlib.crc32(raw[:4], 0xFFFFFFFF) & 0xFFFFFFFF
    return OtaRecord(sector, seq, state, crc, expected_crc)


def seq_is_newer(a: int, b: int) -> bool:
    """ESP-style wrap-safe comparison for non-equal uint32 sequence values."""
    if a == b:
        return False
    return ((a - b) & 0xFFFFFFFF) < 0x80000000


def determine_active_slot(otadata: bytes, ota_apps: list[Partition]) -> tuple[Partition, Partition, list[OtaRecord]]:
    if len(otadata) != OTADATA_SIZE:
        raise RuntimeError("Unexpected otadata size.")
    if len(ota_apps) != 2:
        raise RuntimeError("This staging helper requires exactly two OTA application slots.")

    records = [
        parse_ota_record(otadata[0:OTA_RECORD_SIZE], 0),
        parse_ota_record(
            otadata[FLASH_SECTOR_SIZE : FLASH_SECTOR_SIZE + OTA_RECORD_SIZE],
            1,
        ),
    ]
    valid = [record for record in records if record.valid]
    if not valid:
        raise RuntimeError("No VALID otadata record with a correct CRC; refusing to infer active slot.")

    newest = valid[0]
    for record in valid[1:]:
        if seq_is_newer(record.seq, newest.seq):
            newest = record

    active_index = (newest.seq - 1) % len(ota_apps)
    active = sorted(ota_apps, key=lambda p: p.subtype)[active_index]
    inactive = next(part for part in ota_apps if part != active)
    return active, inactive, records


def validate_factory_layout(entries: list[Partition]) -> tuple[Partition, Partition]:
    by_label = {part.label: part for part in entries}
    app0 = by_label.get("app0")
    app1 = by_label.get("app1")
    otadata = by_label.get("otadata")
    if app0 is None or app1 is None or otadata is None:
        raise RuntimeError("Expected app0, app1 and otadata partitions were not found.")

    if (app0.offset, app0.size) != EXPECTED_APP0:
        raise RuntimeError(
            f"Unexpected app0 layout: offset=0x{app0.offset:x}, size=0x{app0.size:x}."
        )
    if (app1.offset, app1.size) != EXPECTED_APP1:
        raise RuntimeError(
            f"Unexpected app1 layout: offset=0x{app1.offset:x}, size=0x{app1.size:x}."
        )
    if (otadata.offset, otadata.size) != (OTADATA_OFFSET, OTADATA_SIZE):
        raise RuntimeError(
            f"Unexpected otadata layout: offset=0x{otadata.offset:x}, size=0x{otadata.size:x}."
        )
    return app0, app1


def verify_backup(backup: Path, live_meta: bytes) -> str:
    if backup.stat().st_size != FLASH_SIZE:
        raise RuntimeError(
            f"Backup must be exactly {FLASH_SIZE} bytes; got {backup.stat().st_size}."
        )

    backup_bytes = backup.read_bytes()

    # Partition layout is immutable and must still match the original image.
    backup_partition_table = backup_bytes[
        PARTITION_TABLE_OFFSET : PARTITION_TABLE_OFFSET + PARTITION_TABLE_SIZE
    ]
    live_partition_table = live_meta[:PARTITION_TABLE_SIZE]
    if backup_partition_table != live_partition_table:
        raise RuntimeError("Current partition table does not match the supplied full-flash backup.")

    # Sector1 is the untouched app0 rescue record. It is our persistent rollback
    # anchor and must remain byte-identical to the original backup.
    backup_rescue_sector = backup_bytes[
        OTADATA_OFFSET : OTADATA_OFFSET + FLASH_SECTOR_SIZE
    ]
    rescue_rel = OTADATA_OFFSET - PROTECTED_META_OFFSET
    live_rescue_sector = live_meta[rescue_rel : rescue_rel + FLASH_SECTOR_SIZE]
    if backup_rescue_sector != live_rescue_sector:
        raise RuntimeError("app0 rescue otadata sector no longer matches the full-flash backup.")

    digest = hashlib.sha256(backup_bytes).hexdigest()
    digest_file = backup.with_suffix(backup.suffix + ".sha256")
    if digest_file.exists():
        recorded = digest_file.read_text(encoding="utf-8").strip().split()
        if not recorded or recorded[0].lower() != digest.lower():
            raise RuntimeError("Backup SHA-256 file does not match the full-flash backup.")
    print(f"Backup verified: {backup} SHA-256={digest}")
    print("Runtime NVS drift is allowed; partition table and app0 rescue record match.")
    return digest


def esptool_write_app(port: str, offset: int, firmware: Path) -> None:
    cmd = [
        sys.executable,
        "-m",
        "esptool",
        "--port",
        port,
        "write-flash",
        hex(offset),
        str(firmware),
    ]
    subprocess.run(cmd, check=True)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Fail-closed X4 Pro inactive OTA-slot staging helper"
    )
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/cu.usbmodem1101")
    parser.add_argument("--firmware", type=Path, required=True, help="ESP32-S3 application firmware.bin")
    parser.add_argument(
        "--backup",
        type=Path,
        required=True,
        help="Previously captured complete 16 MiB X4 Pro flash backup",
    )
    parser.add_argument(
        "--write",
        action="store_true",
        help="Actually stage firmware into the validated inactive app slot. Without this flag: dry-run only.",
    )
    args = parser.parse_args()

    try:
        if not args.firmware.is_file():
            raise RuntimeError(f"Firmware not found: {args.firmware}")
        if not args.backup.is_file():
            raise RuntimeError(f"Backup not found: {args.backup}")

        print("X4 Pro inactive-slot staging preflight")
        print("======================================")
        identify_chip(args.port)
        check_security(args.port)
        check_firmware_image(args.firmware)

        with tempfile.TemporaryDirectory(prefix="x4pro-stage-") as tmp:
            temp = Path(tmp)
            live_meta_file = temp / "protected-meta-before.bin"
            read_flash(args.port, PROTECTED_META_OFFSET, PROTECTED_META_SIZE, live_meta_file)
            live_meta = live_meta_file.read_bytes()

            partition_raw = live_meta[
                PARTITION_TABLE_OFFSET - PROTECTED_META_OFFSET :
                PARTITION_TABLE_OFFSET - PROTECTED_META_OFFSET + PARTITION_TABLE_SIZE
            ]
            entries = parse_partitions(partition_raw)
            ota_apps = validate_partitions(entries, args.firmware.stat().st_size)
            validate_factory_layout(entries)

            otadata = live_meta[
                OTADATA_OFFSET - PROTECTED_META_OFFSET :
                OTADATA_OFFSET - PROTECTED_META_OFFSET + OTADATA_SIZE
            ]
            active, inactive, records = determine_active_slot(otadata, ota_apps)

            # Staging is deliberately allowed only from the exact factory-style
            # selector state restored by x4pro_boot_slot.py --target app1.
            if not (
                len(records) == 2
                and records[0].valid
                and records[0].seq == 1
                and not records[1].valid
                and records[1].seq == 0xFFFFFFFF
                and records[1].state == 0xFFFFFFFF
                and records[1].crc == 0xFFFFFFFF
            ):
                raise RuntimeError(
                    "Expected factory app0 state: sector0 seq1 VALID and sector1 erased."
                )

            verify_backup(args.backup, live_meta)

            firmware_size = args.firmware.stat().st_size
            firmware_hash = sha256(args.firmware)
            if firmware_size > inactive.size:
                raise RuntimeError(
                    f"Firmware ({firmware_size} bytes) exceeds inactive slot {inactive.label} "
                    f"({inactive.size} bytes)."
                )
            if args.firmware.resolve() == args.backup.resolve():
                raise RuntimeError("Firmware path unexpectedly points to the full-flash backup.")

            print("\notadata:")
            for record in records:
                status = "VALID" if record.valid else "IGNORED"
                print(
                    f"  sector{record.sector}: seq={record.seq} state={record.state} "
                    f"crc=0x{record.crc:08x} ({status})"
                )

            print("\nValidated staging plan:")
            print(
                f"  ACTIVE / PROTECTED: {active.label} @ 0x{active.offset:x}, "
                f"size=0x{active.size:x}"
            )
            print(
                f"  INACTIVE / TARGET:  {inactive.label} @ 0x{inactive.offset:x}, "
                f"size=0x{inactive.size:x}"
            )
            print(f"  Firmware: {args.firmware} ({firmware_size} bytes)")
            print(f"  Firmware SHA-256: {firmware_hash}")
            print("  Bootloader:       NOT WRITTEN")
            print("  Partition table:  NOT WRITTEN")
            print("  NVS:              NOT WRITTEN")
            print("  otadata:          NOT WRITTEN")
            print("  Active app slot:  NOT WRITTEN")
            print("  Boot selection:   UNCHANGED")

            if not args.write:
                print("\nDRY RUN OK")
                print("No flash data was modified. Re-run with --write only after reviewing this plan.")
                return 0

            # This port is intentionally scoped to the known first-test direction:
            # app1 must remain the working image and app0 is the staging target.
            if active.label != "app0" or inactive.label != "app1":
                raise RuntimeError(
                    f"For the guarded first test, expected active=app0/inactive=app1; "
                    f"found active={active.label}/inactive={inactive.label}."
                )

            print("\nSTAGING APPLICATION ONLY...")
            esptool_write_app(args.port, inactive.offset, args.firmware)

            readback = temp / "inactive-app-readback.bin"
            print("\nReading staged bytes back for SHA-256 verification...")
            read_flash(args.port, inactive.offset, firmware_size, readback)
            readback_hash = sha256(readback)
            if readback_hash != firmware_hash:
                raise RuntimeError(
                    f"Readback SHA-256 mismatch: firmware={firmware_hash}, readback={readback_hash}"
                )

            live_meta_after_file = temp / "protected-meta-after.bin"
            print("\nRe-checking partition table, NVS and otadata...")
            read_flash(
                args.port,
                PROTECTED_META_OFFSET,
                PROTECTED_META_SIZE,
                live_meta_after_file,
            )
            if live_meta_after_file.read_bytes() != live_meta:
                raise RuntimeError(
                    "Protected metadata changed during staging. STOP and preserve the full backup."
                )

            print("\nSTAGE VERIFIED")
            print(f"{inactive.label} contains the exact firmware image (SHA-256 {readback_hash}).")
            print(f"{active.label} remains selected and was not written.")
            print("Boot selection was NOT changed, so the staged firmware will NOT boot yet.")
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
            "Do not change otadata or retry with generic upload/erase commands.",
            file=sys.stderr,
        )
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
