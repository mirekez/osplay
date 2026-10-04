#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
exec bash "$root/tests/extreme/run_extreme.sh" --kernel-only "$@"
