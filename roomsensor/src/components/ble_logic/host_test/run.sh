#!/usr/bin/env bash
# Builds and runs the BLE logic's host unit tests with the system C++ compiler.
set -euo pipefail
cd "$(dirname "$0")"
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT
${CXX:-g++} -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -I.. \
    ../ble_scan_logic.cpp test_ble_scan_logic.cpp -o "$OUT/test_ble_scan_logic"
"$OUT/test_ble_scan_logic"
