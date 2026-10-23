#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
out="$repo/.pio/biscuit-native-previews"
mkdir -p "$out"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
c++ -std=c++17 -O2 -Wall -Wextra -Werror -I"$here" \
  "$here/test_preview.cpp" -o "$work/test_preview"
"$work/test_preview" "$out"
