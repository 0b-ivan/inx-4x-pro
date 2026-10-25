#!/bin/sh
# Builds and runs the German catalogue tests against a stub I18n.h.
#
#   host-tests/locali18n/run.sh
set -e
cd "$(dirname "$0")"
BUILD_DIR="${TMPDIR:-/tmp}/$(basename "${CXX:-c++}")-locali18n-tests-$(cd ../.. && pwd | cksum | cut -d" " -f1)"
mkdir -p "$BUILD_DIR"
python3 check_catalogue.py
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -Istub test_locali18n.cpp -o "$BUILD_DIR/test_locali18n"
"$BUILD_DIR/test_locali18n"
