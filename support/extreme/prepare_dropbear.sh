#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
base="${EXTREME_BUILD_ROOT:-$root/build/tests/extreme}"
source="$base/sources/dropbear-extreme"
build="$base/dropbear"
llvm="${CPPHDL_HOME:-$HOME/cpphdl}/.conda/bin"
if [[ ! -d "$source" ]]; then cp -a "$base/sources/dropbear" "$source"; fi
if [[ ! -f "$source/.extreme-patched" ]]; then
  patch -l -d "$source" -p1 < "$root/tests/busybox/patches/dropbear-mikos.patch"
  touch "$source/.extreme-patched"
fi
mkdir -p "$build"
cp "$root/tests/busybox/config/dropbear_localoptions.h" "$build/localoptions.h"
cd "$build"
if [[ -f Makefile ]]; then make -s clean; fi
CC="$root/support/extreme/configure-cc.py" AR="$llvm/llvm-ar" RANLIB="$llvm/llvm-ranlib" \
  CFLAGS="-O2 -isystem $base/sysroot/include" \
  "$source/configure" --host=arm-none-eabi --disable-zlib --disable-syslog \
    --disable-lastlog --disable-utmp --disable-utmpx --disable-wtmp --disable-wtmpx
# Dropbear's final target is a relocatable bitcode module for the image link.
python3 - <<'MAKEFILE'
from pathlib import Path
p = Path("Makefile")
s = p.read_text().replace('$(CC) $(LDFLAGS) -o $@$(EXEEXT)', '$(LD) -o $@$(EXEEXT)')
p.write_text(s)
MAKEFILE
make -j "${JOBS:-2}" PROGRAMS=dropbear CC="$root/support/extreme/cc.py" \
  LD="$root/support/extreme/ld.py" AR="$llvm/llvm-ar" RANLIB="$llvm/llvm-ranlib" \
  CFLAGS="-O2 -isystem $base/sysroot/include -I$source/src -I$build -Dmain=extreme_dropbear_main" dropbear
