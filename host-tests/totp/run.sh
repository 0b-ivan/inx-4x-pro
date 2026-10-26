#!/bin/sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-totp-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_totp.cpp $SRC/totp/TotpCore.cpp -o "$BUILD_DIR/test_totp"
"$BUILD_DIR/test_totp"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_uri.cpp $SRC/totp/TotpCore.cpp $SRC/totp/TotpUri.cpp -o "$BUILD_DIR/test_uri"
"$BUILD_DIR/test_uri"
