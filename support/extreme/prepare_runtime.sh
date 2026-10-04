#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
base="${EXTREME_BUILD_ROOT:-$root/build/tests/extreme}"
source="$root/build/kernel-cxx/compiler-rt-21.1.3.src/lib/builtins"
[[ -d "$source" ]] || bash "$root/support/kernel-cxx/prepare.sh"
build="$base/runtime"
mkdir -p "$build"
objects=()
for name in addsf3 subsf3 mulsf3 divsf3 adddf3 subdf3 muldf3 divdf3 \
  comparesf2 comparedf2 fixsfsi fixunssfsi fixdfsi fixunsdfsi \
  fixsfdi fixunssfdi fixdfdi fixunsdfdi floatsisf floatunsisf \
  floatsidf floatunsidf floatdisf floatundisf floatdidf floatundidf \
  extendsfdf2 truncdfsf2 fp_mode; do
  "$root/support/extreme/cc.py" -c "$source/$name.c" -o "$build/$name.bc"
  objects+=("$build/$name.bc")
done
"${CPPHDL_HOME:-$HOME/cpphdl}/.conda/bin/llvm-link" "${objects[@]}" -o "$build/softfloat.bc"
