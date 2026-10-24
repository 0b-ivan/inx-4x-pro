#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
c++ -std=c++17 -DSIMULATOR -Wall -Wextra -Werror -I"$repo/src" \
  "$here/test_clock.cpp" -o "$work/test_clock"
"$work/test_clock"
c++ -std=c++17 -DSIMULATOR -Wall -Wextra -Werror -I"$repo/src" -I"$repo/sim-stubs" \
  "$here/test_network.cpp" -o "$work/test_network"
"$work/test_network"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$here/test_portal_lab.cpp" -o "$work/test_portal_lab"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$here/test_wardrive_csv.cpp" -o "$work/test_wardrive_csv"
"$work/test_wardrive_csv"
"$work/test_portal_lab"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$here/test_ap_lab.cpp" -o "$work/test_ap_lab"
"$work/test_ap_lab"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$here/test_local_web_text.cpp" -o "$work/test_local_web_text"
"$work/test_local_web_text"
c++ -std=c++17 -Wall -Wextra -Werror -I"$repo/src" \
  "$here/test_wardrive_write.cpp" -o "$work/test_wardrive_write"
"$work/test_wardrive_write"
python3 "$here/test_wiring.py" "$repo"
python3 "$here/test_visual_goldens.py" --self-test
