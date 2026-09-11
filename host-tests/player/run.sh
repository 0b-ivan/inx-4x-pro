#!/bin/sh
# Builds and runs the shared player tests. The domain model and progression
# logic stay freestanding; store/service/auth suites link host SQLite/wolfSSL so
# the same persistence and credential code can be exercised without a device.
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

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  ../../src/apps_local/player/PlayerProgression.cpp test_progression.cpp \
  -o "$BUILD_DIR/test_progression"
"$BUILD_DIR/test_progression"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  ../../src/apps_local/leaderboard/RankSystem.cpp test_rank.cpp \
  -o "$BUILD_DIR/test_rank"
"$BUILD_DIR/test_rank"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  ../../src/apps_local/player/PlayerName.cpp ../../src/apps_local/player/PlayerStore.cpp \
  test_store.cpp -lsqlite3 -o "$BUILD_DIR/test_store"
"$BUILD_DIR/test_store"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  ../../src/apps_local/player/PlayerName.cpp ../../src/apps_local/player/PlayerStore.cpp \
  ../../src/apps_local/player/PlayerStoreDirectory.cpp test_directory.cpp \
  -lsqlite3 -o "$BUILD_DIR/test_directory"
"$BUILD_DIR/test_directory"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  ../../src/apps_local/player/PlayerName.cpp ../../src/apps_local/player/PlayerStore.cpp \
  ../../src/apps_local/player/PlayerAuth.cpp test_auth.cpp \
  -lsqlite3 -lwolfssl -o "$BUILD_DIR/test_auth"
"$BUILD_DIR/test_auth"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  ../../src/apps_local/player/PlayerName.cpp ../../src/apps_local/player/PlayerStore.cpp \
  ../../src/apps_local/player/PlayerAuth.cpp ../../src/apps_local/player/PlayerService.cpp test_service.cpp \
  -lsqlite3 -lwolfssl -o "$BUILD_DIR/test_service"
"$BUILD_DIR/test_service"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  ../../src/apps_local/player/PlayerName.cpp ../../src/apps_local/player/PlayerStore.cpp \
  ../../src/apps_local/player/PlayerAuth.cpp ../../src/apps_local/player/PlayerService.cpp test_registration.cpp \
  -lsqlite3 -lwolfssl -o "$BUILD_DIR/test_registration"
"$BUILD_DIR/test_registration"
