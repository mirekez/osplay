# Shared Simulator Ethernet

Tribe, Extreme and Rocket use the same UNIX-datagram Ethernet framing. Their
interactive launchers use `/tmp/mikos-tap.sock`. Setup and launchers automatically
select the TAP that already owns `192.168.76.1`, even if it is named `tap-extreme`
or `tap-tribe`. If none exists, setup creates `tap-mikos`.
Run only one simulator on a bridge at a time: a simulator's HELLO selects the
peer that receives host traffic. Stop it before starting another simulator.
Keep the bridge running between simulations.

```bash
cd ~/mikos
bash tests/network/build_tap.sh
sudo bash tests/network/start_tap.sh --background
python3 tests/network/tap.py rocket
python3 tests/network/tap.py tribe
python3 tests/network/tap.py extreme
```

Each launcher checks socket accessibility/liveness, interface state, host IP,
route to the guest, and the permanent neighbor entry before starting the CPU.
Checks never send HELLO and do not disturb the current peer. They do not prove
that the guest has booted. `start_tap.sh` can repair IP/neighbor configuration
while reusing an existing live bridge; it refuses stale sockets rather than
deleting files or stopping another process.

## Reuse an Existing Bridge

If the old bridge process has exited, simply run the shared setup command again.
It reuses the idle interface and starts the bridge at the shared socket; no
exports or interface deletion are needed. A TAP attached to another process is
not taken over: stop that bridge or select its existing socket instead.

For example, to reuse the existing Extreme-named bridge with all three CPUs:

```bash
export MIKOS_TAP=tap-extreme
export MIKOS_TAP_SOCKET=/tmp/extreme-ethgig.sock
sudo env MIKOS_TAP="$MIKOS_TAP" MIKOS_TAP_SOCKET="$MIKOS_TAP_SOCKET" \
  bash tests/network/start_tap.sh --background
python3 tests/network/tap.py rocket
tests/rocket/rocket_interactive.sh --no-build
```

Use these exports in the terminal running the simulator. `--tap-socket` overrides
the socket. Platform setup scripts delegate to shared setup.

## Address Profiles

| CPU | Host | Guest | SSH port |
|---|---|---|---|
| Tribe / Rocket | 192.168.76.1/24 | 192.168.76.2 | 22 |
| Extreme | 192.168.76.1/24 | 192.168.76.2 | 2222 |

All profiles use guest MAC `02:00:00:00:00:02` and the same /24 subnet.
No additional subnet, alias or host route is installed for Extreme.
The helper refuses to duplicate `192.168.76.1` on a second interface.

Rebuild existing Extreme guest images before running: older images used a
different address. Run `tests/extreme/extreme_interactive.sh` without `--no-build`
to update them. Rocket and Tribe already use this subnet.

For an isolated link, run setup and the simulator inside the same network
namespace (where unprivileged user namespaces are enabled):

```bash
unshare -Urn bash
# Inside this shell, use a private bridge socket:
export MIKOS_TAP=tap-mikos MIKOS_TAP_SOCKET=/tmp/mikos-private-$$.sock
bash tests/network/start_tap.sh --background
python3 tests/network/tap.py extreme
tests/extreme/extreme_interactive.sh --no-build
```

The shared TAP can be checked without booting a CPU:

```bash
unshare -Urn python3 tests/network/test_tap.py
```

That test uses real Linux TAP traffic and synthetic framed peers for all three
profiles, verifies sequential peer handover, in-place repair, stale sockets and
local-IP conflict detection. It is not an OS simulation test.
