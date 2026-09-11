#!/bin/sh
# Builds and runs the Battleship rules tests. No device and no PlatformIO:
# BattleshipCore is freestanding C++17, which is also what lets two simulated
# devices play a whole game in host-tests/link/.
#
#   host-tests/battleship/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-battleship-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
SRC=../../src/apps_local/battleship
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/BattleshipCore.cpp \
  test_battleship.cpp -o "$BUILD_DIR/test_battleship"
"$BUILD_DIR/test_battleship"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/BattleshipCore.cpp \
  $SRC/web/BrowserSnapshot.cpp test_browser_snapshot.cpp -o "$BUILD_DIR/test_browser_snapshot"
"$BUILD_DIR/test_browser_snapshot"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 $SRC/BattleshipCore.cpp \
  $SRC/../player/PlayerName.cpp $SRC/web/BrowserCommands.cpp $SRC/web/BrowserPlayer.cpp \
  test_browser_commands.cpp -o "$BUILD_DIR/test_browser_commands"
"$BUILD_DIR/test_browser_commands"
python3 test_browser_boundary.py

# Network lifecycle tests do not need the generated, gzipped page bytes.
cat > "$BUILD_DIR/BattleshipPageHtml.generated.h" <<'HEADER'
#pragma once
constexpr char BattleshipPageHtml[]="";
constexpr size_t BattleshipPageHtmlCompressedSize=0;
HEADER
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Wno-narrowing -O2 -Istubs -I"$BUILD_DIR" \
  $SRC/BattleshipCore.cpp $SRC/web/BrowserSnapshot.cpp $SRC/web/BrowserCommands.cpp $SRC/web/BattleshipBrowserServer.cpp \
  stubs/PlayerWebApiStub.cpp test_browser_server.cpp -o "$BUILD_DIR/test_browser_server"
"$BUILD_DIR/test_browser_server"
node test_browser_page.cjs
