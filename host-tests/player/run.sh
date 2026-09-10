#!/bin/sh
# Builds and runs the shared player tests. The domain model and the composing
# half of PlayerName stay freestanding: no storage, Arduino or renderer needed.
#
#   host-tests/player/run.sh
set -e
cd "$(dirname "$0")"
# Keyed to this checkout, not just the suite name. Two worktrees sharing
# one build dir means one tree can run -- and pass -- a binary the other
# built, which is a green suite whose source is not even present.
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-player-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 ../../src/apps_local/player/PlayerName.cpp \
  test_name.cpp -o "$BUILD_DIR/test_name"
"$BUILD_DIR/test_name"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  test_domain.cpp -o "$BUILD_DIR/test_domain"
"$BUILD_DIR/test_domain"
