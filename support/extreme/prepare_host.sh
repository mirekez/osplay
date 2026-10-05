#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
base="${EXTREME_BUILD_ROOT:-$root/build/tests/extreme}"
build="$base/host"
mkdir -p "$build"
"${HOST_CXX:-g++}" -std=c++20 -O2 "$root/tests/qemu/ethgig_tap.cpp" -o "$build/ethgig_tap"
bash "$root/tests/busybox/build_dropbear_host.sh" "$base/sources/dropbear-extreme" \
  "$build" "${JOBS:-2}" "$root/tests/busybox/config/dropbear_localoptions.h"
bash "$root/support/extreme/prepare_ssh_key.sh"
