#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/paths.sh"
socket="${ROCKET_ETH_SOCKET:-${MIKOS_TAP_SOCKET:-/tmp/mikos-tap.sock}}"
tap="${ROCKET_INTERACTIVE_TAP:-${MIKOS_TAP:-}}"
prepare=1
test_mode=
network_hints() {
  echo "Host link: $tap 192.168.76.1/24; Rocket: 192.168.76.2/24, MAC 02:00:00:00:00:02."
  echo 'No gateway is needed between these two addresses.'
  echo 'Inside the Rocket BusyBox console, configure the guest interface if needed:'
  echo '  ifconfig eth0 192.168.76.2 netmask 255.255.255.0 up'
  echo 'From another host terminal, after the guest reaches its shell:'
  echo '  ping -n -i 10 -W 120 192.168.76.2'
  echo 'Dropbear starts automatically during boot; no console command is needed.'
  echo 'Wait for MIKOS:TCP_LISTEN 22, then connect from the host:'
  printf '  make -C %q dropbear-client\n' "$root"
  printf '  %q -i %q -c none -y -y root@192.168.76.2\n' \
    "$root/build/tests/busybox/dropbear-host/dbclient" \
    "$root/build/tests/busybox/dropbear-host/mikos_ssh_key.dropbear"
  echo 'Use that explicit test identity; the default SSH identity will not authenticate.'
  echo 'SSH sessions are serialized. This test-only client disables encryption and host-key prompts.'
  echo 'Rocket SSH is not yet validated end to end; ARP, ICMP and TCP command execution are verified.'
  echo 'RTL boot and image restoration can take many minutes; allow long connection timeouts.'
}
while (($#)); do
  case "$1" in
    --test) test_mode="${2:?--test requires ping, process, tcp or ssh}"; shift ;;
    --tap-socket) socket="${2:?--tap-socket requires a path}"; shift ;;
    --no-build) prepare=0 ;;
    --verilator) : ;;
    --help|-h)
      echo "usage: $0 [--no-build] [--tap-socket PATH] [--test ping|process|tcp|ssh]"; exit 0 ;;
    *) echo "unknown argument: $1 (this profile is single-hart RV32 Rocket)" >&2; exit 2 ;;
  esac
  shift
done
if [[ -n "$test_mode" ]]; then
  if ((prepare)); then bash "$root/tests/rocket/prepare.sh"; fi
  case "$test_mode" in
    ping|process) exec python3 "$root/tests/rocket/test_sim.py" "$test_mode" ;;
    tcp|ssh) exec env ROCKET_ETH_TAP_SOCKET="$socket" bash "$root/tests/rocket/run_interactive_${test_mode}.sh" ;;
    *) echo "unknown test: $test_mode" >&2; exit 2 ;;
  esac
fi
if [[ -z "$tap" ]]; then tap="$(python3 "$root/tests/network/tap.py" --select-tap)"; fi
[[ -S "$socket" ]] || {
  {
    echo "Rocket TAP bridge socket is missing: $socket"
    echo 'Start the bridge as follows; only TAP setup needs sudo:'
    printf '  cd %q\n' "$root"
    echo '  bash tests/network/build_tap.sh'
    printf '  sudo env MIKOS_TAP=%q MIKOS_TAP_SOCKET=%q bash tests/network/start_tap.sh --background\n' "$tap" "$socket"
    echo 'This configures the host IP and permanent guest neighbor entry.'
    echo 'Then rerun this launcher without sudo, with the same options.'
    echo 'If a bridge is already running, use --tap-socket PATH instead of starting another.'
    network_hints
    printf 'Rootless Ethernet test (no TAP or sudo):\n  %q --no-build --test ping\n' "$root/tests/rocket/rocket_interactive.sh"
  } >&2
  exit 1
}
python3 "$root/tests/network/tap.py" rocket --tap "$tap" --socket "$socket"
if ((prepare)); then bash "$root/tests/rocket/prepare.sh"; fi
{
  echo 'Starting mikOS on RV32 Rocket (Verilator).'
  echo "Type 'exit' to stop mikOS; Ctrl+C stops the simulator; Ctrl+Z suspends it."
  echo "Ethernet bridge socket: $socket"
  network_hints
} >&2
saved_tty=
if [[ -t 0 ]]; then saved_tty="$(stty -g)"; stty -icanon -echo min 1 time 0; fi
child=
cleanup() {
  [[ -z "$saved_tty" ]] || stty "$saved_tty"
  if [[ -n "$child" ]]; then kill "$child" 2>/dev/null || true; wait "$child" 2>/dev/null || true; fi
}
trap cleanup EXIT
trap 'exit 130' INT TERM
ROCKET_ETH_SOCKET="$socket" bash "$root/tests/rocket/run_rocket.sh" <&0 &
child=$!
status=0
wait "$child" || status=$?
child=
exit "$status"
