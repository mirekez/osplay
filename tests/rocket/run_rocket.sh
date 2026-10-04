#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/paths.sh"
[[ -x "$simulator" && -f "$kernel" ]] || { echo "Run tests/rocket/prepare.sh first" >&2; exit 1; }
sysroot="$envdir/x86_64-conda-linux-gnu/sysroot"
loader="$sysroot/lib64/ld-linux-x86-64.so.2"
command=("$simulator")
if [[ -x "$loader" ]]; then
  command=("$loader" --library-path "$sysroot/lib64:$sysroot/usr/lib64:$envdir/lib:$envdir/riscv-tools/lib" "$simulator")
fi
# Keep UART stdin open; +loadmem uses the real SimDRAM ELF loader and TSI boot.
exec "${command[@]}" +permissive +max-cycles="${ROCKET_CYCLES:-1000000000}" \
  +loadmem="$kernel" +permissive-off "$kernel" "$@"
