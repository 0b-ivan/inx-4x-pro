#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

BASE_CONFIG="platformio.ini"
PASSKEY_CONFIG="platformio.passkey.ini"
BACKUP="$(mktemp)"

cp "$BASE_CONFIG" "$BACKUP"
restore_config() {
  cp "$BACKUP" "$BASE_CONFIG"
  rm -f "$BACKUP"
}
trap restore_config EXIT HUP INT TERM

# pioarduino's custom_sdkconfig flow performs a second `pio run -e $PIOENV`
# after rebuilding the ESP-IDF libraries. That nested invocation does not keep
# the outer --project-conf argument, so x4pro_passkey must temporarily be
# visible from the default platformio.ini as well.
if ! grep -q '^\[env:x4pro_passkey\]$' "$BASE_CONFIG"; then
  printf '\n' >> "$BASE_CONFIG"
  sed -n '/^\[env:x4pro_passkey\]$/,$p' "$PASSKEY_CONFIG" >> "$BASE_CONFIG"
fi

python3 -m platformio run -e x4pro_passkey
