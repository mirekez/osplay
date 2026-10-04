#!/usr/bin/env bash
# Compatibility entry point: all CPUs share the same TAP setup.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
exec bash "$root/tests/network/start_tap.sh" "$@"
