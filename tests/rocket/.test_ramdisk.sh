#!/usr/bin/env bash
# Create/format/mount a file-backed disk entirely in the real RV32 Rocket guest.
set -euo pipefail
test_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$test_dir/paths.sh"
if [[ "${1:-}" == --build ]]; then
  bash "$test_dir/prepare.sh"
  shift
fi
make -C "$root" -j"${ROCKET_JOBS:-2}" rocket-ramdisk-kernel \
  CXX="$host_clang" LLVM_READELF="$readelf"
export ROCKET_KERNEL="$root/build/osplay-rocket-ramdisk-rv32.elf"
exec python3 "$test_dir/test_sim.py" ramdisk "$@"
