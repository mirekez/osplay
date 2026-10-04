#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
base="${EXTREME_BUILD_ROOT:-$root/build/tests/extreme}"
source="$base/sources/busybox"
build="$base/busybox"
llvm="${CPPHDL_HOME:-$HOME/cpphdl}/.conda/bin"
[[ -f "$base/libc/lib/libc.a" ]] || bash "$root/support/extreme/prepare_libc.sh"
mkdir -p "$build"
if [[ -f "$build/Makefile" ]]; then make -s -C "$source" O="$build" clean; fi
make -s -C "$source" O="$build" HOSTCC=gcc allnoconfig
python3 - "$build/.config" <<'CONFIG'
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text()
for name in ("LFS", "TIME64", "BUSYBOX", "USE_PORTABLE_CODE", "ASH", "ASH_BASH_COMPAT", "ASH_JOB_CONTROL", "ASH_ALIAS", "ASH_ECHO", "ASH_PRINTF", "ASH_TEST", "ASH_GETOPTS", "ASH_CMDCMD", "FEATURE_SH_MATH", "FEATURE_SH_MATH_64", "SH_IS_ASH", "ECHO", "PRINTF", "TEST", "TEST1", "TEST2", "CAT", "LS", "MKDIR", "PWD", "SLEEP", "TRUE", "FALSE", "PING", "FEATURE_FANCY_PING", "IFCONFIG", "NC", "NC_SERVER", "NC_EXTRA", "KILL", "NETSTAT", "FEATURE_NETSTAT_WIDE", "FEATURE_NETSTAT_PRG", "TOP", "FEATURE_TOP_CPU_USAGE_PERCENTAGE", "TTY", "FEATURE_SH_STANDALONE", "FEATURE_PREFER_APPLETS"):
    s = s.replace(f"# CONFIG_{name} is not set", f"CONFIG_{name}=y")
p.write_text(s)
CONFIG
make -s -C "$source" O="$build" HOSTCC=gcc oldconfig </dev/null
make -C "$source" O="$build" HOSTCC=gcc CC="$root/support/extreme/cc.py" \
  LD="$root/support/extreme/ld.py" AR="$llvm/llvm-ar" NM="$llvm/llvm-nm" \
  EXTRA_CFLAGS="-isystem $base/sysroot/include -Dmain=extreme_busybox_main" \
  'cmd_busybox__=$(LD) -o $@ $(core-y) $(libs-y)' \
  -j "${JOBS:-2}" busybox_unstripped
