#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
source "$root/tests/extreme/paths.sh"
python3 "$root/tests/extreme/test_net_peer.py" "$build/net_peer"
