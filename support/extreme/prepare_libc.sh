#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
base="${EXTREME_BUILD_ROOT:-$root/build/tests/extreme}"
source="$base/sources/musl-1.2.5"
archive="$base/sources/musl-1.2.5.tar.gz"
mkdir -p "$base/sources"
if [[ ! -f "$archive" ]]; then
  curl --fail --location --retry 3 https://musl.libc.org/releases/musl-1.2.5.tar.gz -o "$archive.part"
  mv "$archive.part" "$archive"
fi
printf '%s  %s\n' a9a118bbe84d8764da0ea0d28b3ab3fae8477fc7e4085d90102b8596fc7c75e4 "$archive" | sha256sum --check --status
[[ -f "$source/Makefile" ]] || tar -xzf "$archive" -C "$base/sources"
mkdir -p "$source/arch/extreme"
cp -a "$source/arch/riscv32/." "$source/arch/extreme/"
cp "$source/arch/arm/bits/float.h" "$source/arch/extreme/bits/float.h"
cp "$root/support/extreme/musl/"*.h "$source/arch/extreme/"
cat > "$source/arch/extreme/bits/setjmp.h" <<'HEADER'
#ifndef EC_BUS_BYTES
#define EC_BUS_BYTES 64
#endif
typedef unsigned long __jmp_buf[(3 * EC_BUS_BYTES + 16) / 4];
HEADER
mkdir -p "$source/src/thread/extreme"
cp "$root/support/extreme/musl/thread/"*.c "$source/src/thread/extreme/"
build="$base/libc"
mkdir -p "$build"
llvm="${CPPHDL_HOME:-$HOME/cpphdl}/.conda/bin"
make -s -C "$build" -f "$source/Makefile" srcdir="$source" ARCH=extreme clean
make -C "$build" -f "$source/Makefile" srcdir="$source" ARCH=extreme \
  CC="$root/support/extreme/cc.py" AR="$llvm/llvm-ar" RANLIB="$llvm/llvm-ranlib" \
  CFLAGS='-O2' -j "${JOBS:-2}" lib/libc.a

make -C "$build" -f "$source/Makefile" srcdir="$source" ARCH=extreme \
  prefix="$base/sysroot" install-headers
"$root/support/extreme/cc.py" -c "$root/support/extreme/libc.c" \
  -I "$build/obj/include" -I "$source/arch/extreme" -I "$source/arch/generic" \
  -I "$source/src/include" -I "$source/src/internal" -I "$source/include" \
  -o "$build/platform.bc"
