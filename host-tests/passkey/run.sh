#!/bin/sh
set -e
cd "$(dirname "$0")"
ROOT="$(cd ../.. && pwd)"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-passkey-tests-$(printf '%s' "$ROOT" | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC="$ROOT/src/apps_local"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -O2 -pthread -I$SRC \
  test_passkey_core.cpp \
  "$SRC/passkey/PasskeyCbor.cpp" \
  "$SRC/passkey/PasskeyPresence.cpp" \
  -o "$BUILD_DIR/test_passkey_core"
"$BUILD_DIR/test_passkey_core"

# v1.12.25 publishes the passkey variant beside the normal X4 Pro firmware.
# The stable updater contract stays on firmware.bin while passkey devices use
# the separate firmware-passkey.bin asset from the same SemVer release.
WF="$ROOT/.github/workflows/crossplay-release.yml"
TAGH="$ROOT/src/network/FirmwareBoardTag.h"
PASSKEY_INI="$ROOT/platformio.passkey.ini"

test -f "$WF"
test ! -e "$ROOT/.github/workflows/crossplay-passkey-release.yml"
grep -q 'dist/firmware.bin' "$WF"
grep -q 'dist/firmware-passkey.bin' "$WF"
grep -q 'tools_local/build_passkey.sh' "$WF"
grep -q 'x4pro-passkey-full.bin' "$WF"
grep -q '#define CROSSPOINT_RELEASE_ASSET "firmware-passkey.bin"' "$TAGH"
grep -q '#define CROSSPOINT_RELEASE_ASSET "firmware.bin"' "$TAGH"
grep -q 'CROSSPOINT_VERSION=\\"${crossplay.version}\\"' "$PASSKEY_INI"
grep -q '^\[base_common\]$' "$ROOT/platformio.ini"
grep -q '^\[env:x4pro_passkey\]$' "$PASSKEY_INI"
grep -q '^  ${base_common.build_flags}$' "$PASSKEY_INI"
grep -q '^  -DARDUINO_USB_MODE=0$' "$PASSKEY_INI"
grep -q '^  -DARDUINO_USB_CDC_ON_BOOT=0$' "$PASSKEY_INI"
! grep -q '^  ${x4pro_common.build_flags}$' "$PASSKEY_INI"
