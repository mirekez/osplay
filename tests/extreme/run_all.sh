#!/usr/bin/env bash
set -uo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
kernel_only=0
if [[ "${1:-}" == --kernel-only ]]; then
  kernel_only=1
  shift
fi
if (($#)); then
  echo "usage: $0 [--kernel-only]" >&2
  exit 2
fi
results="${EXTREME_BUILD_ROOT:-$root/build/tests/extreme}/results"
mkdir -p "$results"
: > "$results/summary.txt"
failed=0
run_case() {
  local name="$1"
  shift
  if "$@" >"$results/$name.log" 2>&1; then
    echo "PASS: $name" | tee -a "$results/summary.txt"
  else
    failed=1
    echo "FAIL: $name (see $results/$name.log)" | tee -a "$results/summary.txt"
    tail -n 12 "$results/$name.log"
  fi
}
for flow in cpp verilator; do
  for cores in single multi; do
    args=()
    [[ "$flow" == verilator ]] && args+=(--verilator)
    [[ "$cores" == multi ]] && args+=(--multicore)
    run_case "boot-$cores-$flow" bash "$root/tests/extreme/run_boot.sh" "${args[@]}"
    run_case "ethernet-$cores-$flow" bash "$root/tests/extreme/run_kernel.sh" --stalls "${args[@]}"
  done
done
run_case peer bash "$root/tests/extreme/run_peer_test.sh"
if ((!kernel_only)); then
  for flow in cpp verilator; do
    for cores in single multi; do
      args=()
      [[ "$flow" == verilator ]] && args+=(--verilator)
      [[ "$cores" == multi ]] && args+=(--multicore)
      for profile in boot descriptors process ping tcp ssh; do
        run_case "userspace-$profile-$cores-$flow" \
          python3 "$root/tests/extreme/userspace.py" --test "$profile" "${args[@]}"
      done
    done
  done
  run_case shared-bridge-build bash "$root/tests/network/build_tap.sh"
  run_case interactive-link unshare -Urn python3 "$root/tests/extreme/test_interactive_network.py"
fi
exit "$failed"
