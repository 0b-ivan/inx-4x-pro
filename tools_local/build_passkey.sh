#!/usr/bin/env bash
set -euo pipefail

python3 -m platformio run \
  --project-conf platformio.passkey.ini \
  -e x4pro_passkey
