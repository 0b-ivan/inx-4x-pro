#!/bin/sh
# Freestanding C++17 game and authenticated seat transport host tests.
# Automatically discovered by .github/workflows/crossplay-ci.yml
set -eu
cd "$(dirname "$0")"
mkdir -p /tmp/cardparty-host-tests
for name in cardparty sessions; do
  "${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
    -I../../src/apps_local/cardparty "test_$name.cpp" \
    -o "/tmp/cardparty-host-tests/test_$name"
  "/tmp/cardparty-host-tests/test_$name"
done
