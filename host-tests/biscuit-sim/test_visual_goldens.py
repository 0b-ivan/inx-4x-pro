#!/usr/bin/env python3
"""Pixel-exact SDL screenshot regression against approved real-run baselines.

Baselines are SHA-256 digests of normalized monochrome pixels from two green
GitHub Actions runs. Unlike a screenshot-exists or before/after check, this
catches any changed pixel in the full Activity display. No Pillow required.
"""
from __future__ import annotations

import hashlib
import json
import struct
import sys
from pathlib import Path


GOLDENS = Path(__file__).with_name("visual_goldens.json")


def read_bmp(path: Path, width: int, height: int, threshold: int) -> bytes:
    content = path.read_bytes()
    if len(content) < 54 or content[:2] != b"BM":
        raise ValueError("missing/invalid BMP header")
    offset = struct.unpack_from("<I", content, 10)[0]
    actual_width, actual_height = struct.unpack_from("<ii", content, 18)
    planes, bpp = struct.unpack_from("<HH", content, 26)
    compression = struct.unpack_from("<I", content, 30)[0]
    if (actual_width, abs(actual_height)) != (width, height):
        raise ValueError(f"unexpected dimensions: {actual_width}x{actual_height}")
    if planes != 1 or bpp != 32 or compression not in (0, 3):
        raise ValueError(f"unexpected BMP format: {planes=} {bpp=} {compression=}")
    if offset < 54 or len(content) < offset + width * height * 4:
        raise ValueError("truncated pixel data")
    raw = memoryview(content)[offset:offset + width * height * 4]
    # SDL screenshots use 32-bit BGRA with grayscale/monochrome pixels.
    # Comparing only B after threshold deliberately ignores BMP headers/alpha.
    return bytes(1 if raw[i] >= threshold else 0 for i in range(0, len(raw), 4))


def quadrants(pixels: bytes, width: int, height: int) -> list[int]:
    # Positive-height BMPs store rows bottom-first. Return visual TL,TR,BL,BR.
    half_y, half_x = height // 2, width // 2
    out: list[int] = []
    for row_range in (range(half_y, height), range(0, half_y)):
        for x0, x1 in ((0, half_x), (half_x, width)):
            out.append(sum(pixels[y * width + x0:y * width + x1].count(0)
                           for y in row_range))
    return out


def self_test() -> None:
    width, height = 2, 2
    header = bytearray(54)
    header[:2] = b"BM"
    struct.pack_into("<I", header, 2, 54 + width * height * 4)
    struct.pack_into("<I", header, 10, 54)
    struct.pack_into("<I", header, 14, 40)
    struct.pack_into("<ii", header, 18, width, height)
    struct.pack_into("<HH", header, 26, 1, 32)
    black = b"\x00\x00\x00\xff"
    white = b"\xff\xff\xff\xff"
    # bottom row: black/white; top row: white/black
    sample = bytes(header) + black + white + white + black
    from tempfile import TemporaryDirectory
    with TemporaryDirectory() as tmp:
        path = Path(tmp) / "sample.bmp"
        path.write_bytes(sample)
        pixels = read_bmp(path, width, height, 128)
        assert pixels == bytes([0, 1, 1, 0])
        assert quadrants(pixels, width, height) == [0, 1, 1, 0]
        other = bytearray(sample)
        other[-8] = 0  # turn one white pixel black
        path.write_bytes(other)
        assert read_bmp(path, width, height, 128) != pixels
        path.write_bytes(sample[:62])
        try:
            read_bmp(path, width, height, 128)
            raise AssertionError("truncated screenshot was accepted")
        except ValueError:
            pass
    print("PASS visual-golden checker self-test (pixels, quadrants, truncation)")


def check(folder: Path) -> None:
    spec = json.loads(GOLDENS.read_text(encoding="utf-8"))
    assert spec["$schema"] == "biscuit-sdl-visual-goldens-v1"
    width, height, threshold = (spec[k] for k in ("width", "height", "threshold"))
    failed = []
    for name, approved in spec["screenshots"].items():
        path = folder / name
        try:
            pixels = read_bmp(path, width, height, threshold)
        except (OSError, ValueError) as exc:
            failed.append(f"{name}: {exc}")
            continue
        actual_hash = hashlib.sha256(pixels).hexdigest()
        actual_ink = pixels.count(0)
        actual_quadrants = quadrants(pixels, width, height)
        if (actual_hash != approved["sha256"] or
                actual_ink != approved["black_pixels"] or
                actual_quadrants != approved["quadrants"]):
            failed.append(
                f"{name}: PIXEL REGRESSION\n"
                f"  expected sha256: {approved['sha256']}\n"
                f"  actual sha256:   {actual_hash}\n"
                f"  expected ink: {approved['black_pixels']}, actual ink: {actual_ink}\n"
                f"  expected TL/TR/BL/BR: {approved['quadrants']}\n"
                f"  actual TL/TR/BL/BR:   {actual_quadrants}\n"
                f"  Review the BMP in the biscuit-simulator-smoke CI artifact. "
                f"Do not silently regenerate baselines."
            )
        else:
            print(f"PASS visual golden {name} ({actual_ink} ink pixels)")
    if failed:
        raise AssertionError("\n\n".join(failed))
    print(f"PASS visual goldens: {len(spec['screenshots'])} real SDL images, pixel exact")


if __name__ == "__main__":
    if len(sys.argv) == 2 and sys.argv[1] == "--self-test":
        self_test()
    elif len(sys.argv) == 2:
        check(Path(sys.argv[1]))
    else:
        raise SystemExit("usage: test_visual_goldens.py --self-test | SCREENSHOT_DIR")
