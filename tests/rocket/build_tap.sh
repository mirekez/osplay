#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/paths.sh"
bash "$root/tests/network/build_tap.sh"
install -m755 "$root/build/tests/network/ethgig_tap" "$build/ethgig_tap"
