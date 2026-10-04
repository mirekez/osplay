#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/paths.sh"
if [[ -z "${ROCKET_INTERACTIVE_TAP:-}${ROCKET_ETH_SOCKET:-}" ]]; then
  exec bash "$root/tests/network/start_tap.sh" "$@"
fi
tap="${ROCKET_INTERACTIVE_TAP:-${MIKOS_TAP:-tap-mikos}}"
socket="${ROCKET_ETH_SOCKET:-${MIKOS_TAP_SOCKET:-/tmp/mikos-tap.sock}}"
bridge="$build/ethgig_tap"
background=0
[[ "${1:-}" != --background ]] || { background=1; shift; }
[[ $# == 0 ]] || { echo "usage: $0 [--background]" >&2; exit 2; }
[[ -x "$bridge" ]] || { echo "Run tests/rocket/build_tap.sh as your normal user first" >&2; exit 1; }
[[ ! -e "$socket" ]] || { echo "Socket exists; stop its owner first: $socket" >&2; exit 1; }
if ! ip link show dev "$tap" >/dev/null 2>&1; then
  ip tuntap add dev "$tap" mode tap user "${SUDO_UID:-$UID}"
fi
ip address replace 192.168.76.1/24 dev "$tap"
ip link set dev "$tap" up
ip neigh replace 192.168.76.2 lladdr 02:00:00:00:00:02 nud permanent dev "$tap"
child=
cleanup() { if [[ -n "$child" ]]; then kill "$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true; fi; }
trap cleanup EXIT
trap 'exit 130' INT TERM
if ((background)); then
  setsid "$bridge" --tap "$tap" --socket "$socket" >"$build/tap.log" 2>&1 </dev/null &
else
  "$bridge" --tap "$tap" --socket "$socket" &
fi
child=$!
for _ in {1..100}; do
  [[ ! -S "$socket" ]] || break
  kill -0 "$child" 2>/dev/null || { echo 'TAP bridge failed' >&2; exit 1; }
  sleep 0.05
done
[[ -S "$socket" ]] || exit 1
chown "${SUDO_UID:-$UID}:${SUDO_GID:-$(id -g)}" "$socket"
chmod 0600 "$socket"
echo "Rocket TAP ready: $tap, $socket; stop with: kill $child"
if ((background)); then child=; else wait "$child"; child=; fi
