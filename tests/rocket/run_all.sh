#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
python3 "$here/test_launcher.py"
python3 "$here/test_softfloat_make.py"
bash "$here/prepare.sh"
bash "$here/run_peer_test.sh"
bash "$here/run_boot.sh"
bash "$here/run_interactive_ping.sh"
bash "$here/run_interactive_process.sh"
# Host TCP/SSH require an explicitly provisioned TAP, unlike the rootless cases.
if [[ "${ROCKET_TEST_TAP:-0}" == 1 ]]; then
  bash "$here/run_interactive_tcp.sh"
  bash "$here/run_interactive_ssh.sh"
fi
