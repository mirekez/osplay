#!/usr/bin/env bash
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)
output="$root/build/tests/network"
mkdir -p "$output"
flags=()
if command -v pkg-config >/dev/null && pkg-config --exists slirp; then
  read -r -a flags <<< "$(pkg-config --cflags --libs slirp)"
else
  deps=${SLIRP_ROOT:-"$output/slirp"}
  if [[ ! -f "$deps/usr/include/slirp/libslirp.h" ]]; then
    [[ -z "${SLIRP_ROOT:-}" ]] || { echo "libslirp headers missing in $SLIRP_ROOT" >&2; exit 2; }
    command -v apt >/dev/null && command -v dpkg-deb >/dev/null || {
      echo 'Install libslirp development files, or set SLIRP_ROOT to an extracted package prefix' >&2; exit 2;
    }
    mkdir -p "$deps/packages"
    (cd "$deps/packages" && apt download libslirp0 libslirp-dev)
    for package in "$deps/packages/"*.deb; do dpkg-deb -x "$package" "$deps"; done
  fi
  lib=$(dirname "$(find "$deps/usr/lib" -name libslirp.so -print -quit)")
  [[ -n "$lib" && -f "$lib/libslirp.so" ]] || { echo 'libslirp library missing' >&2; exit 2; }
  flags=("-I$deps/usr/include" "-L$lib" "-Wl,-rpath,$lib" -lslirp)
fi
"${HOST_CXX:-g++}" -std=c++20 -O2 -Wall -Wextra -Werror -Wno-deprecated-declarations \
  "$root/tests/network/ethgig_proxy.cpp" -o "$output/ethgig_proxy" "${flags[@]}"
