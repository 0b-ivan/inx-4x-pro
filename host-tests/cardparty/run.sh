#!/bin/sh
# Freestanding C++17 rules/seat/wire protocol + network and browser smoke.
set -eu
cd "$(dirname "$0")"
mkdir -p /tmp/cardparty-host-tests
for name in cardparty sessions protocol full_match; do
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
/tmp/cardparty-host-tests/test_browser_server
node test_browser_page.cjs
