#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
cpu="${EXTREME_CPU_HOME:-$root/tests/extreme/extreme_cpu}"
base="${EXTREME_BUILD_ROOT:-$root/build/tests/extreme}"
llvm="${CPPHDL_HOME:-$HOME/cpphdl}/.conda/bin"
compiler="${EXTREME_COMPILER:-$cpu/build/cmake/bin/extreme-cxx}"
cxx="$root/build/kernel-cxx"
build="${EXTREME_USER_BUILD:-$base/userspace}"
bits="${EXTREME_BITS:-512}"
words="${EXTREME_BANK_WORDS:-524288}"
mkdir -p "$build"
application_flags=()
applications=("$base/busybox/busybox_unstripped")
if [[ -n "${EXTREME_TEST_APPLICATION:-}" ]]; then
  "$root/support/extreme/cc.py" -isystem "$base/sysroot/include" \
    -c "$EXTREME_TEST_APPLICATION" -o "$build/test-main.bc"
  applications=("$build/test-main.bc")
fi
if [[ "${EXTREME_DROPBEAR:-1}" == 1 ]]; then
  [[ -f "$base/dropbear/dropbear" ]] || { echo 'Build Dropbear with support/extreme/prepare_dropbear.sh first.' >&2; exit 1; }
  application_flags=(-DEXTREME_DROPBEAR=1)
  applications+=("$base/dropbear/dropbear")
fi
[[ "$bits" == 512 ]] || { echo 'The initial userspace profile requires the 32-call return capacity of EC_BITS=512.' >&2; exit 1; }
sources=(support/extreme/kernel_runtime.cpp kernel/arch/extreme/userspace.cpp kernel/syscall.cpp kernel/pseudo_filesystem.cpp
         network/stack.cpp drivers/net/extreme.cpp drivers/uart/extreme.cpp
         drivers/storage/extreme.cpp drivers/fs/root.cpp)
objects=()
for source in "${sources[@]}"; do
  object="$build/${source//\//_}.bc"
  "$llvm/clang++" --target=armv7-none-eabi -mfloat-abi=soft -std=c++2c -O2 \
    -ffreestanding -fno-exceptions -fno-rtti -fno-threadsafe-statics \
    -fno-vectorize -fno-slp-vectorize -fno-unroll-loops -nostdinc++ \
    -isystem "$cxx/libcxx-21.1.3.src/include" -isystem "$base/sysroot/include" \
    -include "$cpu/compiler/include/extreme/libcxx_config.h" \
    -I "$cxx/inplace_vector-63569fe8502c3504d7d81bb6d378f3ec21fb1e95/include" \
    -I "$cxx/include" -I "$root" -I "$root/include" -I "$cpu" -I "$cpu/compiler" \
    -I "$root/support/extreme/include" \
    "${application_flags[@]}" -DOSPLAY_EXTREME=1 -DOSPLAY_EXTREME_USERSPACE=1 -DEC_BUS_BYTES=64 \
    -DEC_BANKS=2 -DEC_BANK_WORDS="$words" -c -emit-llvm "$root/$source" -o "$object"
  objects+=("$object")
done
musl="$base/sources/musl-1.2.5"
"$root/support/extreme/cc.py" -c "$root/support/extreme/libc.c" \
  -I "$base/libc/obj/include" -I "$musl/arch/extreme" -I "$musl/arch/generic" \
  -I "$musl/src/include" -I "$musl/src/internal" -I "$musl/include" -o "$build/libc-init.bc"
"$root/support/extreme/cc.py" -c "$root/support/extreme/application.c" \
  "${application_flags[@]}" -I "$base/sysroot/include" -I "$root/support/extreme/include" -o "$build/application.bc"
"$llvm/llvm-link" "${objects[@]}" "$build/libc-init.bc" "$build/application.bc" \
  "${applications[@]}" -o "$build/main.bc"
"$llvm/llvm-link" --only-needed "$build/main.bc" "$base/libc/lib/libc.a" -o "$build/linked.bc"
"$compiler" "$build/linked.bc" "$base/runtime/softfloat.bc" --frontend armv7 --lto \
  --bits "$bits" --banks 2 --bank-words "$words" --reentrant \
  -o "$build/osplay.ecx" --emit-ir "$build/osplay.ll"
