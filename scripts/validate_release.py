#!/usr/bin/env python3
"""Validate that an X4 Pro release tag matches the firmware version."""

import argparse
import configparser
import re
from pathlib import Path


TAG_RE = re.compile(r"^v(?P<version>\d+\.\d+\.\d+)-x4pro-alpha\.(?P<alpha>\d+)$")


def expected_tag(platformio: Path) -> str:
    config = configparser.ConfigParser(interpolation=None)
    config.read(platformio, encoding="utf-8")
    version = config["inx"]["version"].strip()
    alpha = config["inx"]["x4pro_prerelease"].strip()
    return f"v{version}-x4pro-alpha.{alpha}"


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--tag", required=True)
    parser.add_argument("--platformio", type=Path, default=Path("platformio.ini"))
    args = parser.parse_args()

    expected = expected_tag(args.platformio)
    if not TAG_RE.fullmatch(args.tag):
        raise SystemExit(f"invalid X4 Pro release tag: {args.tag}; expected format: {expected}")
    if args.tag != expected:
        raise SystemExit(f"release tag {args.tag} does not match firmware version; expected {expected}")
    print(f"release tag matches firmware version: {expected}")


if __name__ == "__main__":
    main()
