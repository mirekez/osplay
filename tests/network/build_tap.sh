#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$root/build/tests/network"
"${HOST_CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Werror \
  "$root/tests/qemu/ethgig_tap.cpp" -o "$root/build/tests/network/ethgig_tap"
