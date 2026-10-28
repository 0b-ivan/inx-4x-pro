#!/bin/sh
# Freestanding C++17 rules/seat/wire protocol, network adapter, 8-browser stress,
# TCP back-pressure, browser smoke and real-browser viewport layout.
set -eu
cd "$(dirname "$0")"
build=$(mktemp -d "${TMPDIR:-/tmp}/cardparty-host-tests.XXXXXX")
trap 'rm -rf "$build"' EXIT HUP INT TERM
for name in cardparty sessions protocol full_match disconnect_progress sd_pack catalog text_wrap; do
  c++ -std=c++17 -funsigned-char -Wall -Wextra -Werror -pedantic -O2 \
    -I../../src/apps_local/cardparty "test_$name.cpp" \
    -o "$build/test_$name"
  "$build/test_$name"
done
cat > "$build"/CardPartyPageHtml.generated.h <<'HEADER'
#pragma once
constexpr char CardPartyPageHtml[]="";
constexpr size_t CardPartyPageHtmlCompressedSize=0;
HEADER
c++ -std=c++17 -funsigned-char -Wall -Wextra -Werror -pedantic -O2 \
  -I../battleship/stubs -I"$build" \
  ../../src/apps_local/cardparty/web/CardPartyBrowserServer.cpp \
  test_browser_server.cpp -o "$build"/test_browser_server
"$build"/test_browser_server
c++ -std=c++17 -funsigned-char -Wall -Wextra -Werror -pedantic -O2 \
  -I../battleship/stubs -I"$build" \
  ../../src/apps_local/cardparty/web/CardPartyBrowserServer.cpp \
  test_backpressure.cpp -o "$build"/test_backpressure
"$build"/test_backpressure
c++ -std=c++17 -funsigned-char -Wall -Wextra -Werror -pedantic -O2 \
  -I../battleship/stubs -I"$build" \
  ../../src/apps_local/cardparty/web/CardPartyBrowserServer.cpp \
  test_stress.cpp -o "$build"/test_stress
"$build"/test_stress
node test_browser_page.cjs
node test_browser_viewport.cjs
python3 test_x4pro_ws_capacity.py
python3 -m unittest discover -s ../../tools_local/cardparty_l10n -p 'test_*.py'
