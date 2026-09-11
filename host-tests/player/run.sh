#!/bin/sh
# Builds and runs the shared player tests. The domain model and progression
# logic stay freestanding; store/service/auth suites use the same binary player
# store as the device and wolfSSL for PIN credentials.
#
#   host-tests/player/run.sh
set -e
cd "$(dirname "$0")"
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
  test_store.cpp -o "$BUILD_DIR/test_store"
"$BUILD_DIR/test_store"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  ../../src/apps_local/player/PlayerName.cpp ../../src/apps_local/player/PlayerStore.cpp \
  test_directory.cpp -o "$BUILD_DIR/test_directory"
"$BUILD_DIR/test_directory"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  ../../src/apps_local/player/PlayerName.cpp ../../src/apps_local/player/PlayerStore.cpp \
  ../../src/apps_local/player/PlayerAuth.cpp test_auth.cpp \
  -lwolfssl -o "$BUILD_DIR/test_auth"
"$BUILD_DIR/test_auth"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  ../../src/apps_local/player/PlayerName.cpp ../../src/apps_local/player/PlayerStore.cpp \
  ../../src/apps_local/player/PlayerAuth.cpp ../../src/apps_local/player/PlayerService.cpp test_service.cpp \
  -lwolfssl -o "$BUILD_DIR/test_service"
"$BUILD_DIR/test_service"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -O2 \
  ../../src/apps_local/player/PlayerName.cpp ../../src/apps_local/player/PlayerStore.cpp \
  ../../src/apps_local/player/PlayerAuth.cpp ../../src/apps_local/player/PlayerService.cpp test_registration.cpp \
  -lwolfssl -o "$BUILD_DIR/test_registration"
"$BUILD_DIR/test_registration"
