#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
multicore=0
if [[ "${1:-}" == --multicore ]]; then multicore=1; shift; fi
if (($#)); then echo "usage: $0 [--multicore]" >&2; exit 2; fi
source "$root/tests/extreme/paths.sh"
cmake -S "$EXTREME_CPU_HOME" -B "$cpu_build" -DCMAKE_BUILD_TYPE=Release \
  -DCPPHDL_HOME="$CPPHDL_HOME" -DEC_BITS="$bits" -DEC_CORES="$cores" \
  -DEC_BANKS=2 -DEC_BOARD_BANK_WORDS="$words" -DEC_SIMD=OFF
cmake --build "$cpu_build" -j "${JOBS:-2}" --target extreme-codegen ExtremeBoard_cpp
mkdir -p "$build"
bash "$root/support/kernel-cxx/prepare.sh"
cxx_root="$root/build/kernel-cxx"
sysroot="${EXTREME_SYSROOT:-${RISCV_SYSROOT:-$HOME/riscv/sysroot}}"
[[ -f "$sysroot/usr/include/stdio.h" ]] || {
  echo "Target C headers missing: set EXTREME_SYSROOT (no target libraries are linked)" >&2
  exit 1
}
"${HOST_CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -I "$root/include" \
  "$root/tests/extreme/net_peer.cpp" -o "$build/net_peer"
for mode in boot kernel; do
  defines=(-DMIKOS_EXTREME=1)
  [[ "$mode" == boot ]] && defines+=(-DMIKOS_EXTREME_BOOT_ONLY=1)
  ((multicore)) && defines+=(-DMIKOS_EXTREME_MULTICORE=1)
  sources=()
  if [[ "$mode" == kernel ]]; then
    for name in cxx_smoke cxx_arena cxx_runtime cxx_math; do
      sources+=("$root/kernel/$name.cpp")
    done
    sources+=("$root/tests/extreme/cxx_math.cpp" "$root/kernel/arch/extreme/runtime.cpp" "$cxx_root/libcxx-21.1.3.src/src/hash.cpp")
    for name in comparesf2 divsf3 mulsf3 fixunssfsi floatunsisf; do
      sources+=("$cxx_root/compiler-rt-21.1.3.src/lib/builtins/$name.c")
    done
  fi
  "$cpu_build/bin/extreme-cxx" "$root/kernel/arch/extreme/boot.cpp" \
    "$root/drivers/uart/extreme.cpp" "$root/drivers/net/extreme.cpp" \
    "${sources[@]}" --reentrant --sysroot "$sysroot" \
    -I "$cxx_root/include" --libcxx-include "$cxx_root/libcxx-21.1.3.src/include" \
    -I "$cxx_root/inplace_vector-63569fe8502c3504d7d81bb6d378f3ec21fb1e95/include" \
    -I "$root" -I "$root/include" -I "$EXTREME_CPU_HOME" \
    --bits "$bits" --banks 2 --bank-words "$words" "${defines[@]}" \
    -o "$build/mikos-$mode.ecx" --emit-ir "$build/mikos-$mode.ll"
done
