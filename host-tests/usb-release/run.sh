#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
python3 host-tests/usb-release/test_config.py
