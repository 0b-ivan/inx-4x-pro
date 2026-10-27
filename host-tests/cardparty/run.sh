#!/bin/sh
# Freestanding C++17 rules/seat/wire protocol, network adapter, 8-browser stress,
# TCP back-pressure, browser smoke and real-browser viewport layout.
set -eu
cd "$(dirname "$0")"
mkdir -p /tmp/cardparty-host-tests
for name in cardparty sessions protocol full_match disconnect_progress sd_pack; do
  c++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
    -I../../src/apps_local/cardparty "test_$name.cpp" \
    -o "/tmp/cardparty-host-tests/test_$name"
  "/tmp/cardparty-host-tests/test_$name"
done
cat > /tmp/cardparty-host-tests/CardPartyPageHtml.generated.h <<'HEADER'
#pragma once
constexpr char CardPartyPageHtml[]="";
constexpr size_t CardPartyPageHtmlCompressedSize=0;
HEADER
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -I../battleship/stubs -I/tmp/cardparty-host-tests \
  ../../src/apps_local/cardparty/web/CardPartyBrowserServer.cpp \
  test_browser_server.cpp -o /tmp/cardparty-host-tests/test_browser_server
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -I../battleship/stubs -I/tmp/cardparty-host-tests \
  ../../src/apps_local/cardparty/web/CardPartyBrowserServer.cpp \
  test_stress.cpp -o /tmp/cardparty-host-tests/test_stress
/tmp/cardparty-host-tests/test_stress
/tmp/cardparty-host-tests/test_browser_server
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -I../battleship/stubs -I/tmp/cardparty-host-tests \
  ../../src/apps_local/cardparty/web/CardPartyBrowserServer.cpp \
  test_backpressure.cpp -o /tmp/cardparty-host-tests/test_backpressure
/tmp/cardparty-host-tests/test_backpressure
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -I../battleship/stubs -I/tmp/cardparty-host-tests \
  ../../src/apps_local/cardparty/web/CardPartyBrowserServer.cpp \
  test_stress.cpp -o /tmp/cardparty-host-tests/test_stress
/tmp/cardparty-host-tests/test_stress
node test_browser_page.cjs
node test_browser_viewport.cjs
python3 test_x4pro_ws_capacity.py
