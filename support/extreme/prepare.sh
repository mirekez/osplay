#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cpu="${EXTREME_CPU_HOME:-$root/tests/extreme/extreme_cpu}"
cpphdl="${CPPHDL_HOME:-$HOME/cpphdl}"
base="${EXTREME_BUILD_ROOT:-$root/build/tests/extreme}"
cores=1
verilator=0
while (($#)); do
  case "$1" in
    --cores) cores="$2"; shift ;;
    --verilator) verilator=1 ;;
    *) echo "usage: $0 [--cores 1|3] [--verilator]" >&2; exit 2 ;;
  esac
  shift
done
[[ "$cores" == 1 || "$cores" == 3 ]] || exit 2
build="$base/userspace-c$cores"
cmake -S "$cpu" -B "$build/cpu" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_COMPILER="${HOST_CXX:-/usr/bin/g++}" -DCPPHDL_HOME="$cpphdl" \
  -DEC_BITS=512 -DEC_CORES="$cores" -DEC_BANKS=2 -DEC_BOARD_BANK_WORDS=524288 -DEC_SIMD=ON
cmake --build "$build/cpu" --target extreme-codegen ExtremeBoard_cpp -j "${JOBS:-2}"
bash "$root/support/kernel-cxx/prepare.sh"
mkdir -p "$base/sources"
if [[ ! -f "$base/sources/busybox/Makefile" ]]; then
  bash "$root/tests/busybox/download_busybox.sh" "$base/sources/busybox" ead17e7808236be80614d0a8755f4ddad63ab56c
fi
if [[ ! -f "$base/sources/dropbear/Makefile.in" ]]; then
  bash "$root/tests/busybox/download_dropbear.sh" "$base/sources/dropbear" 2026.94 \
    e098034a843699200c8c977a991fff73159735bf795d5f72ef672c41a6b1ae81
fi
stage=(python3 "$root/support/extreme/stage.py")
"${stage[@]}" --stamp "$base/libc.stamp" --artifact "$base/libc/platform.bc" \
  --input "$root/support/extreme/prepare_libc.sh" --input "$root/support/extreme/cc.py" \
  --input "$root/support/extreme/libc.c" --input "$root/support/extreme/musl" \
  -- bash "$root/support/extreme/prepare_libc.sh"
"${stage[@]}" --stamp "$base/runtime.stamp" --artifact "$base/runtime/softfloat.bc" \
  --input "$root/support/extreme/prepare_runtime.sh" --input "$root/support/extreme/cc.py" \
  -- bash "$root/support/extreme/prepare_runtime.sh"
"${stage[@]}" --stamp "$base/busybox.stamp" --artifact "$base/busybox/busybox_unstripped" \
  --input "$root/support/extreme/prepare_busybox.sh" --input "$root/support/extreme/cc.py" \
  --input "$root/support/extreme/ld.py" --input "$base/libc.stamp" \
  -- bash "$root/support/extreme/prepare_busybox.sh"
"${stage[@]}" --stamp "$base/dropbear.stamp" --artifact "$base/dropbear/dropbear" \
  --input "$root/support/extreme/prepare_dropbear.sh" --input "$root/support/extreme/cc.py" \
  --input "$root/support/extreme/configure-cc.py" --input "$root/support/extreme/ld.py" \
  --input "$root/tests/busybox/config/dropbear_localoptions.h" --input "$base/libc.stamp" \
  --input "$root/tests/busybox/patches/dropbear-osplay.patch" \
  -- bash "$root/support/extreme/prepare_dropbear.sh"
"${stage[@]}" --stamp "$base/host.stamp" --artifact "$base/host/dbclient" \
  --input "$root/support/extreme/prepare_host.sh" --input "$root/tests/qemu/ethgig_tap.cpp" \
  --input "$root/tests/busybox/config/dropbear_localoptions.h" \
  --input "$root/tests/busybox/patches/dropbear-osplay.patch" \
  -- bash "$root/support/extreme/prepare_host.sh"
"${stage[@]}" --stamp "$base/userspace.stamp" --artifact "$base/userspace/osplay.ecx" \
  --input "$root/support/extreme" --input "$root/kernel" --input "$root/include" \
  --input "$root/drivers" --input "$root/network" --input "$cpu/compiler" \
  --input "$build/cpu/compiler/extreme-codegen" --input "$build/cpu/bin/extreme-cxx" \
  --input "$base/libc.stamp" --input "$base/runtime.stamp" \
  --input "$base/busybox.stamp" --input "$base/dropbear.stamp" \
  -- env EXTREME_COMPILER="$build/cpu/bin/extreme-cxx" \
  bash "$root/support/extreme/prepare_userspace.sh"
if ((verilator)); then
  python3 "$cpu/scripts/board.py" --build-dir "$build/rtl" --bits 512 --cores "$cores" \
    --banks 2 --bank-words 524288 --simd --cpphdl "$cpphdl" --cxx "${HOST_CXX:-/usr/bin/g++}" --build-only
fi
