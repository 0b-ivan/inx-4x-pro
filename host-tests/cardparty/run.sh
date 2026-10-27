#!/bin/sh
# Host-only C++17 rules. This script is discovered by crossplay-ci.yml.
set -eu
cd "$(dirname "$0")"
mkdir -p /tmp/cardparty-host-tests
c++ -std=c++17 -Wall -Wextra -Werror -pedantic -O2 \
  -I../../src/apps_local/cardparty test_cardparty.cpp \
  -o /tmp/cardparty-host-tests/test_cardparty
/tmp/cardparty-host-tests/test_cardparty
