#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
multicore=0
mode=kernel
flow=cpp
stalls=0
prepare=1
while (($#)); do
  case "$1" in
    --multicore) multicore=1 ;;
    --boot-only) mode=boot ;;
    --kernel-only) mode=kernel ;;
    --verilator) flow=verilator ;;
    --stalls) stalls=1 ;;
    --no-build) prepare=0 ;;
    *) echo "unknown argument: $1" >&2; exit 2 ;;
  esac
  shift
done
source "$root/tests/extreme/paths.sh"
args=()
((multicore)) && args+=(--multicore)
if ((prepare)); then bash "$root/tests/extreme/prepare_cpphdl.sh" "${args[@]}"; fi
runtime="$(mktemp -d /tmp/mikos-extreme.XXXXXX)"
peer_pid=""
cleanup() {
  if [[ -n "$peer_pid" ]]; then kill "$peer_pid" 2>/dev/null || true; wait "$peer_pid" 2>/dev/null || true; fi
  rm -rf "$runtime"
}
trap cleanup EXIT
log="$build/$mode-$flow.log"
peer_log="$build/$mode-$flow-peer.log"
args=(--image "$build/mikos-$mode.ecx" --cycles "${EXTREME_CYCLES:-20000000}")
((multicore)) && args+=(--require-task)
((stalls)) && args+=(--stalls)
if [[ "$mode" == kernel ]]; then
  "$build/net_peer" "$runtime/ethernet.sock" >"$peer_log" 2>&1 &
  peer_pid=$!
  for ((attempt=0; attempt<500; ++attempt)); do
    [[ -S "$runtime/ethernet.sock" ]] && break
    kill -0 "$peer_pid" 2>/dev/null || break
    sleep 0.01
  done
  [[ -S "$runtime/ethernet.sock" ]] || { cat "$peer_log"; exit 1; }
  args+=(--socket "$runtime/ethernet.sock")
fi
simulator="$cpu_build/devices/ExtremeBoard_cpp"
if [[ "$flow" == verilator ]]; then
  python3 "$EXTREME_CPU_HOME/scripts/board.py" --build-only --build-dir "$build/rtl" \
    --bits "$bits" --cores "$cores" --bank-words "$words" --cpphdl "$CPPHDL_HOME"
  simulator="$build/rtl/obj/board"
fi
status=0
timeout "${EXTREME_TIMEOUT:-600}" "$simulator" "${args[@]}" >"$log" 2>&1 || status=$?
if ((status)); then cat "$log"; [[ -e "$peer_log" ]] && cat "$peer_log"; exit "$status"; fi
if [[ -n "$peer_pid" ]]; then
  for ((attempt=0; attempt<200; ++attempt)); do kill -0 "$peer_pid" 2>/dev/null || break; sleep 0.01; done
  if kill -0 "$peer_pid" 2>/dev/null; then echo 'peer did not validate Ethernet reply' >&2; exit 1; fi
  wait "$peer_pid" || { cat "$peer_log"; exit 1; }
  peer_pid=""
fi
for marker in MIKOS:BOOT MIKOS:EXTREME_CXX_OK MIKOS:FLAT_DEVICE_IRQ_OFF 'MIKOS:EXIT 0'; do
  grep -Fxq "$marker" "$log" || { cat "$log"; echo "missing marker: $marker"; exit 1; }
done
if [[ "$mode" == kernel ]]; then
  grep -Fxq 'MIKOS:KERNEL_CXX_OK' "$log"
  grep -Fxq 'MIKOS:ARP_REPLY' "$log"
  grep -Fxq 'MIKOS:ICMP_ECHO_REPLY' "$log"
fi
cat "$log"
echo "PASS: Extreme mikOS $mode flow=$flow cores=$cores"
