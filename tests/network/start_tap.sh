#!/usr/bin/env bash
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
tap="$(python3 "$root/tests/network/tap.py" --select-tap)"
socket="${MIKOS_TAP_SOCKET:-/tmp/mikos-tap.sock}"
bridge="$root/build/tests/network/ethgig_tap"
background=0
if [[ "${1:-}" == --background ]]; then background=1; shift; fi
[[ $# == 0 ]] || { echo "usage: $0 [--background]" >&2; exit 2; }
[[ -x "$bridge" ]] || { echo "Run: bash $root/tests/network/build_tap.sh" >&2; exit 1; }
existing=0
if [[ -e "$socket" || -L "$socket" ]]; then
  "$bridge" --probe-socket "$socket" || {
    echo "Stale or inaccessible socket: $socket; inspect its owner before removing it." >&2
    exit 1
  }
  existing=1
fi
if ((!existing)) && ip -j link show | python3 -c 'import json,sys; sys.exit(not any(l["ifname"] == sys.argv[1] and "LOWER_UP" in l["flags"] for l in json.load(sys.stdin)))' "$tap"; then
  echo "TAP $tap already has an attached process; select its socket with MIKOS_TAP_SOCKET or stop that bridge first." >&2
  exit 1
fi
if ! ip link show dev "$tap" >/dev/null 2>&1; then
  if ((existing)); then echo "Existing bridge selected but TAP $tap is missing." >&2; exit 1; fi
  ip tuntap add dev "$tap" mode tap user "${SUDO_UID:-$UID}"
fi
ip -j -d link show dev "$tap" | python3 -c 'import json,sys; l=json.load(sys.stdin)[0].get("linkinfo", {}); sys.exit(not (l.get("info_kind") == "tun" and l.get("info_data", {}).get("type") == "tap"))' || {
  echo "Not a TAP interface: $tap" >&2; exit 1;
}
ip -j address show | python3 -c 'import json,sys; tap=sys.argv[1]; conflicts=[l["ifname"] for l in json.load(sys.stdin) if l["ifname"] != tap and any(a.get("local") == "192.168.76.1" for a in l["addr_info"])]; print("Host IP 192.168.76.1 already belongs to " + ", ".join(conflicts) + "; select that TAP with MIKOS_TAP and its socket with MIKOS_TAP_SOCKET.", file=sys.stderr) if conflicts else None; sys.exit(bool(conflicts))' "$tap"
ip address replace 192.168.76.1/24 dev "$tap"
ip link set dev "$tap" up
ip neigh replace 192.168.76.2 lladdr 02:00:00:00:00:02 nud permanent dev "$tap"

if ((existing)); then
  echo "Reusing shared bridge: $tap, $socket (no new process started)."
  exit 0
fi
child=
cleanup() { if [[ -n "$child" ]]; then kill "$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true; fi; }
trap cleanup EXIT
trap 'exit 130' INT TERM
log="$root/build/tests/network/tap.log"
if ((background)); then
  setsid "$bridge" --tap "$tap" --socket "$socket" >"$log" 2>&1 </dev/null &
else
  "$bridge" --tap "$tap" --socket "$socket" &
fi
child=$!
for _ in {1..100}; do
  [[ ! -S "$socket" ]] || break
  kill -0 "$child" 2>/dev/null || { echo "Bridge exited; see $log" >&2; exit 1; }
  sleep 0.05
done
[[ -S "$socket" ]] || { echo 'Bridge socket did not appear' >&2; exit 1; }
chown "${SUDO_UID:-$UID}:${SUDO_GID:-$(id -g)}" "$socket"
chmod 0600 "$socket"
echo "Shared TAP ready: $tap, $socket; one active simulator at a time."
echo "Log: $log; stop with: sudo kill $child"
if ((background)); then child=; else wait "$child"; child=; fi
