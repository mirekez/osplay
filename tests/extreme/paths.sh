#!/usr/bin/env bash
# Sourced by Extreme test entry points.
EXTREME_CPU_HOME="${EXTREME_CPU_HOME:-$root/tests/extreme/extreme_cpu}"
CPPHDL_HOME="${CPPHDL_HOME:-$HOME/cpphdl}"
bits="${EXTREME_BITS:-128}"
words="${EXTREME_BANK_WORDS:-16384}"
cores=1
[[ "${multicore:-0}" == 1 ]] && cores=3
build="${EXTREME_BUILD_ROOT:-$root/build/tests/extreme}/b${bits}-c${cores}"
cpu_build="$build/cpu"
