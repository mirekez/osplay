#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
exec bash "$root/tests/extreme/extreme_interactive.sh" --test ssh "$@"
