#!/bin/sh
# Builds and runs the OTA release catalog tests. No device and no PlatformIO.
set -eu
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-ota-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
ROOT=../..
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic \
  -I"$ROOT/lib/JsonParser" \
  -I"$ROOT/src/apps_local/ota" \
  -I"$ROOT/src/network" \
  "$ROOT/lib/JsonParser/StreamingJsonParser.cpp" \
  "$ROOT/src/apps_local/ota/OtaReleaseCatalog.cpp" \
  test_ota_release_catalog.cpp \
  -o "$BUILD_DIR/test_ota_release_catalog"
"$BUILD_DIR/test_ota_release_catalog"
