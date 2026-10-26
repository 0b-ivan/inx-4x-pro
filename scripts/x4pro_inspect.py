#!/usr/bin/env python3
"""Read-only preflight inspector for the Xteink X4 Pro.

This script intentionally contains NO erase/write/boot-selection commands. It:
  * asks esptool to identify the connected chip,
  * reads the partition-table sector,
  * parses and validates OTA application slots,
  * optionally creates a full 16 MiB flash backup and SHA-256 digest.

Nothing produced by this script is flashed back automatically.
"""

from __future__ import annotations

import argparse
import hashlib
import struct
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

FLASH_SIZE = 0x1000000
PARTITION_TABLE_OFFSET = 0x8000
PARTITION_TABLE_SIZE = 0x1000
PARTITION_MAGIC = 0x50AA


@dataclass(frozen=True)
class Partition:
    type: int
    subtype: int
    offset: int
    size: int
    label: str
    flags: int

    @property
    def end(self) -> int:
        return self.offset + self.size

    @property
    def is_ota_app(self) -> bool:
        # ESP-IDF app type=0x00; OTA subtypes ota_0..ota_15 are 0x10..0x1f.
        return self.type == 0x00 and 0x10 <= self.subtype <= 0x1F


def run_esptool(port: str, command: str, *args: str, capture: bool = False) -> subprocess.CompletedProcess[str]:
    """Run a read-only esptool command, accepting v4/v5 command spellings."""
    spellings = [command, command.replace("-", "_")]
    last: subprocess.CompletedProcess[str] | None = None
    for spelling in dict.fromkeys(spellings):
        cmd = [sys.executable, "-m", "esptool", "--port", port, spelling, *args]
        last = subprocess.run(cmd, text=True, capture_output=capture)
        if last.returncode == 0:
            return last
    assert last is not None
    if capture:
        sys.stderr.write(last.stdout)
        sys.stderr.write(last.stderr)
    raise RuntimeError(f"esptool command failed: {command}")


def identify_chip(port: str) -> None:
    result = run_esptool(port, "chip-id", capture=True)
    text = f"{result.stdout}\n{result.stderr}"
    print(text.strip())
    if "ESP32-S3" not in text.upper().replace("ESP32-S3", "ESP32-S3"):
        # Avoid accepting an X4/C3 by accident. esptool output varies by version,
        # so use a conservative textual check and fail closed.
        if "ESP32-S3" not in text:
            raise RuntimeError("Connected chip was not identified as ESP32-S3; refusing to continue.")


def read_flash(port: str, offset: int, size: int, destination: Path) -> None:
    destination.parent.mkdir(parents=True, exist_ok=True)
    run_esptool(port, "read-flash", hex(offset), hex(size), str(destination))
    actual = destination.stat().st_size
    if actual != size:
        raise RuntimeError(f"Short read for {destination}: expected {size} bytes, got {actual}")


def parse_partitions(raw: bytes) -> list[Partition]:
    entries: list[Partition] = []
    entry_size = 32
    for pos in range(0, len(raw) - entry_size + 1, entry_size):
        entry = raw[pos : pos + entry_size]
        magic = struct.unpack_from("<H", entry, 0)[0]
        if magic in (0xFFFF, 0x0000):
            break
        if magic != PARTITION_MAGIC:
            # MD5/checksum metadata or trailing bytes begin after the entries.
            break
        p_type, subtype, offset, size = struct.unpack_from("<BBII", entry, 2)
        label_raw = entry[12:28].split(b"\0", 1)[0]
        label = label_raw.decode("ascii", errors="replace")
        flags = struct.unpack_from("<I", entry, 28)[0]
        entries.append(Partition(p_type, subtype, offset, size, label, flags))
    return entries


def validate_partitions(entries: list[Partition], firmware_size: int | None) -> list[Partition]:
    if not entries:
        raise RuntimeError("No valid ESP-IDF partition entries found at 0x8000.")

    for part in entries:
        if part.offset < 0 or part.size <= 0 or part.end > FLASH_SIZE:
            raise RuntimeError(f"Partition {part.label!r} is outside the 16 MiB flash boundary.")

    ordered = sorted(entries, key=lambda p: p.offset)
    for previous, current in zip(ordered, ordered[1:]):
        if previous.end > current.offset:
            raise RuntimeError(f"Partition overlap: {previous.label!r} and {current.label!r}.")

    ota_apps = sorted((p for p in entries if p.is_ota_app), key=lambda p: p.subtype)
    if len(ota_apps) < 2:
        raise RuntimeError("Expected at least two OTA app slots; refusing a single-slot layout.")

    if firmware_size is not None:
        too_small = [p for p in ota_apps if firmware_size > p.size]
        if too_small:
            labels = ", ".join(f"{p.label} ({p.size} bytes)" for p in too_small)
            raise RuntimeError(f"firmware.bin ({firmware_size} bytes) does not fit: {labels}")

    return ota_apps


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main() -> int:
    parser = argparse.ArgumentParser(description="Read-only Xteink X4 Pro flash preflight")
    parser.add_argument("--port", required=True, help="Serial port, e.g. /dev/cu.usbmodemXXXX")
    parser.add_argument("--out", type=Path, default=Path("x4pro-preflight"), help="Output directory")
    parser.add_argument("--firmware", type=Path, help="Optional firmware.bin to size-check only; never flashed")
    parser.add_argument("--backup", action="store_true", help="Read the complete 16 MiB flash to a backup file")
    args = parser.parse_args()

    try:
        identify_chip(args.port)

        partition_file = args.out / "partition-table.bin"
        print(f"\nReading partition table -> {partition_file}")
        read_flash(args.port, PARTITION_TABLE_OFFSET, PARTITION_TABLE_SIZE, partition_file)
        entries = parse_partitions(partition_file.read_bytes())

        firmware_size = None
        if args.firmware is not None:
            firmware_size = args.firmware.stat().st_size
            print(f"Firmware size check only: {args.firmware} = {firmware_size} bytes")

        ota_apps = validate_partitions(entries, firmware_size)

        print("\nPartition table:")
        print("label            type sub  offset      size        end")
        for p in entries:
            print(f"{p.label[:16]:16}  {p.type:02x}   {p.subtype:02x}   0x{p.offset:08x}  0x{p.size:08x}  0x{p.end:08x}")

        print("\nValidated OTA application slots:")
        for p in ota_apps:
            print(f"  {p.label}: offset=0x{p.offset:x}, size=0x{p.size:x} ({p.size} bytes)")

        if args.backup:
            backup = args.out / "x4pro-full-16mb.bin"
            print(f"\nReading complete flash -> {backup}")
            read_flash(args.port, 0, FLASH_SIZE, backup)
            digest = sha256(backup)
            digest_file = backup.with_suffix(backup.suffix + ".sha256")
            digest_file.write_text(f"{digest}  {backup.name}\n", encoding="utf-8")
            print(f"SHA-256: {digest}")

        print("\nREAD-ONLY PREFLIGHT OK")
        print("No flash data, boot selector, partition table, NVS or OTA metadata was modified.")
        return 0
    except (OSError, RuntimeError, subprocess.SubprocessError) as exc:
        print(f"\nPREFLIGHT FAILED: {exc}", file=sys.stderr)
        print("Nothing was written to the device.", file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
