#!/bin/sh
# Builds and runs the ConnectFour rules tests. ConnectFourCore is freestanding C++17.
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-connectfour-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/connectfour
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC test_connectfour.cpp -o "$BUILD_DIR/test_connectfour"
"$BUILD_DIR/test_connectfour"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 -I$SRC \
  test_browser_adapter.cpp -o "$BUILD_DIR/test_browser_adapter"
"$BUILD_DIR/test_browser_adapter"

node test_browser_page.cjs

cat > "$BUILD_DIR/ConnectFourBrowserPageHtml.generated.h" <<'HEADER'
#pragma once
#include <cstddef>
constexpr char ConnectFourBrowserPageHtml[]="";
constexpr size_t ConnectFourBrowserPageHtmlCompressedSize=0;
HEADER
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-narrowing -O2 -I../battleship/stubs -I"$BUILD_DIR" \
  $SRC/web/ConnectFourBrowserServer.cpp test_browser_server.cpp -o "$BUILD_DIR/test_browser_server"
"$BUILD_DIR/test_browser_server"
