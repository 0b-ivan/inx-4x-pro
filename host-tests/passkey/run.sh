#!/bin/sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-passkey-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -O2 -pthread -I$SRC \
  test_passkey_core.cpp \
  $SRC/passkey/PasskeyCbor.cpp \
  $SRC/passkey/PasskeyPresence.cpp \
  -o "$BUILD_DIR/test_passkey_core"
"$BUILD_DIR/test_passkey_core"
