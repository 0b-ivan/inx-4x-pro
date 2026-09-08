#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")/../.."
BUILD=$(mktemp -d)
trap 'rm -rf "$BUILD"' EXIT
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -Isrc/apps_local/rss host-tests/rss/test_time.cpp src/apps_local/rss/RssTime.cpp -o "$BUILD/rss-time"
"$BUILD/rss-time"
${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -Ihost-tests/rss/stubs -Isrc/apps_local/rss host-tests/rss/test_wifi.cpp src/apps_local/rss/RssWifi.cpp -o "$BUILD/rss-wifi"
"$BUILD/rss-wifi"
${CXX:-c++} -std=c++20 -Wall -Wextra -Wno-unused-variable -DSIMULATOR -Ihost-tests/rss/stubs -Isrc/apps_local/rss -Ilib/RssParser -Ilib/XmlParserUtils -Itest/opds_feed/stubs host-tests/rss/test_sync.cpp src/apps_local/rss/RssSync.cpp src/apps_local/rss/RssWifi.cpp src/apps_local/rss/RssTime.cpp lib/RssParser/RssParser.cpp -lexpat -o "$BUILD/rss-sync"
"$BUILD/rss-sync"

${CXX:-c++} -std=c++20 -Wall -Wextra -Werror -Ihost-tests/rss/stubs -Isrc/apps_local/rss -Ilib/RssParser -Itest/opds_feed/stubs host-tests/rss/test_cache.cpp src/apps_local/rss/RssCache.cpp -o "$BUILD/rss-cache"
"$BUILD/rss-cache"
