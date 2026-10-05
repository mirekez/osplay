#!/usr/bin/env bash
# Use the same identity and converted client key as the Tribe/Rocket rootfs.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
base="${EXTREME_BUILD_ROOT:-$root/build/tests/extreme}"
key="$root/build/mikos_ssh_key"
output="$root/build/tests/busybox/dropbear-host/mikos_ssh_key.dropbear"

# These key-only targets do not build the RISC-V programs or require a toolchain.
make --no-print-directory -s -C "$root/tests/busybox" "$key.pub"
mkdir -p "$(dirname "$output")"
temporary="$(mktemp -d "$(dirname "$output")/key-conversion.XXXXXX")"
trap 'rm -rf "$temporary"' EXIT
"$base/host/dropbearconvert" openssh dropbear "$key" "$temporary/key"
chmod 600 "$temporary/key"
if [[ ! -f "$output" ]] || ! cmp -s "$temporary/key" "$output"; then
  mv -f "$temporary/key" "$output"
fi
chmod 600 "$output"
