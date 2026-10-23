#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
c++ -std=c++17 -DSIMULATOR -Wall -Wextra -Werror -I"$repo/src" \
  "$here/test_clock.cpp" -o "$work/test_clock"
"$work/test_clock"
c++ -std=c++17 -DSIMULATOR -Wall -Wextra -Werror -I"$repo/src" \
  "$here/test_network.cpp" -o "$work/test_network"
"$work/test_network"
python3 "$here/test_wiring.py" "$repo"
