#!/usr/bin/env python3
"""Refuse generic PlatformIO uploads for the experimental X4 Pro port.

A normal PlatformIO upload can write more than the application partition. Until
we have inspected the live X4 Pro partition table and implemented an inactive-OTA
slot flasher, all hardware writes are deliberately blocked.
"""

import sys

print("ERROR: Generic PlatformIO upload is disabled for the X4 Pro port.", file=sys.stderr)
print("Do not flash bootloader.bin or partitions.bin.", file=sys.stderr)
print("Use the future validated inactive-OTA-slot flashing path instead.", file=sys.stderr)
sys.exit(2)
