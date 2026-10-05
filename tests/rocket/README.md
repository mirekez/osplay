# mikOS on Rocket

This profile runs the real Chipyard **RV32 Rocket RTL in Verilator**, not an ISA
emulator. mikOS and its current Linux-compatible userspace ABI are RV32. This
does not claim RV64 userspace support or a C++HDL simulator backend.

## Build and test

```bash
cd ~/mikos
export MIKOS_CXX=~/cpphdl/.conda/bin/clang++
tests/rocket/prepare.sh
tests/rocket/run_all.sh
```

All scripts use the `chipyard` symlink beside them in `tests/rocket/`, regardless
of the shell's working directory. It points to `/home/me/chipyard/chipyard` in
this workspace. To select another checkout, update that symlink;
`CHIPYARD_HOME` is no longer used.

Prerequisites: an initialized Chipyard checkout with `ethernet.patch` installed,
its `.conda-env` (Verilator with timing support, host GCC, firtool and FESVR),
the RV32 Linux toolchain described in the main README, and the existing mikOS
kernel-C++ dependencies. `prepare.sh` installs only `MikosRocket.scala` and
`MikosNetBridge.sv` into Chipyard, then builds the kernel/rootfs and simulator.
It does not run Chipyard's destructive environment/bootstrap setup script.
`--kernel-only` and `--sim-only` build either side separately. `ROCKET_JOBS`
defaults to two to bound compilation memory. `MIKOS_READELF`, `CHIPYARD_ENV`,
and the existing `RISCV_HOME`/`RISCV_LINUX_PREFIX` variables override tools.

The rootless `run_interactive_ping.sh` sends real Ethernet ARP and ICMP requests
through the simulator socket, validates reply MAC/IP addresses, checksums and
512 payload bytes, exercises the UART BusyBox shell, and requires a clean HTIF
exit. `run_interactive_process.sh` exercises ext4 writes, shell commands and
processes. `ROCKET_TIMEOUT` is a wall-time bound (default 3600 seconds for
boot/ping, 7200 for the longer process test, and 3600 per TCP/SSH test stage);
`ROCKET_CYCLES` is a separate simulated-cycle bound. Logs are under
`build/tests/rocket/`. `run_boot.sh` deliberately stops the simulator after the
boot/C++/platform-state markers; it is not a full shutdown test.
Full userspace startup can take tens of minutes on a slow host. The test runner
prints boot milestones; use `tail -f build/tests/rocket/ping.log` for UART output.

## Interactive Ethernet

```bash
bash tests/network/build_tap.sh
sudo bash tests/network/start_tap.sh --background
tests/rocket/rocket_interactive.sh --no-build
```

The default bridge is shared with Tribe and Extreme: `tap-mikos`, socket
`/tmp/mikos-tap.sock`. See [shared TAP setup](../network/TAP.md) for reusing
an existing bridge via `MIKOS_TAP` and `MIKOS_TAP_SOCKET`. Only one simulator
may use a bridge at a time. The launcher checks the live socket, interface,
host IP, route and permanent guest neighbor before building/starting Rocket.

The launcher prints host TAP setup instructions when the bridge is missing,
including any custom socket and TAP name. At startup it also prints the guest
`ifconfig`, host `ping`, and optional Dropbear client commands. Only TAP setup
needs sudo; run the simulator as your normal user.

`rocket_interactive.sh` launches the Rocket console. `prepare_cpphdl.sh`
preserves the Tribe script entry-point name but builds Verilator.
The guest is `192.168.76.2/24`, MAC `02:00:00:00:00:02`; the host TAP is
`192.168.76.1/24`. Rocket opens a login shell that sources `/etc/profile` and
starts Dropbear automatically. Wait for `MIKOS:TCP_LISTEN 22` before connecting
from the host; no console setup command is needed. Starting the background
server entails a full BusyBox image restoration and can delay the first shell
prompt by several minutes in RTL simulation. Type `exit` at the console to stop the guest. UART stdin is
not redirected away.
The early `MIKOS:NET_IP 10.0.2.15` banner precedes interactive interface
configuration; use `192.168.76.2` for this profile.
Use `--tap-socket PATH` or `ROCKET_ETH_SOCKET` to select a different bridge.
`ROCKET_INTERACTIVE_TAP` selects a host TAP name. Only TAP provisioning needs
network-administration privileges; the simulator runs as the normal user.

Optional host TCP/SSH tests use the provisioned TAP. The SSH test logs out of
an interactive PTY with `exit`, reconnects to run a command, and checks that
the UART shell still responds before explicitly stopping the guest:

```bash
tests/rocket/run_interactive_tcp.sh
tests/rocket/run_interactive_ssh.sh
# Include them in the suite:
ROCKET_TEST_TAP=1 tests/rocket/run_all.sh
```

## Platform

- One RV32IMAC/F Rocket hart, 50 MHz buses, 256 MiB DRAM at `0x80000000`.
- Kernel in M-mode; BusyBox/Dropbear in U-mode with PMP protection. No SBI or
  virtual-memory translation is required by this mikOS port.
- Startup explicitly clears `medeleg`/`mideleg` and initializes counter access.
  Rocket's delegation registers are not reset to the emulator defaults; leaving
  them untouched can route the first userspace syscall to an undefined S-mode
  trap vector.
- SiFive UART at `0x10020000`, 1 Mbaud, polling RX/TX. The RX read consumes a
  byte, so the driver caches a byte between `ready()` and `receive()`.
- CLINT at `0x02000000`, 50 kHz physical timebase; timer driver exposes the
  shared kernel's 10 MHz time units. PLIC sources are masked; networking polls.
- Writable ext4 RAM disk embedded at `0x84000000`. Changes are private to the
  simulation and disappear on restart; this is not a hardware SD driver.
- ELF loading and exit use Chipyard SimDRAM `+loadmem` and TSI/HTIF; `tohost`
  and `fromhost` reside outside userspace at `0x83000000`.
- Ethernet is a raw-frame 64-bit ready/valid stream with coherent FBUS DMA.
  No PHY, PCIe endpoint, or host packet bypass around the guest driver exists.
  A DPI UNIX-datagram bridge uses the same frame protocol as `tests/tribe` and
  the existing TAP helper: hello byte 1; frames byte 2, big-endian length16,
  then Ethernet bytes. Short host frames are padded to 60 bytes. No FCS.

### RV32 NIC registers

Base `0x10016000`; all accesses are 32-bit. This staged ABI is distinct from
the original RV64 IceNIC descriptor ABI. DMA engines and coherency are reused.

| Offset | Access | Meaning |
|---|---|---|
| 0x00 | RW | Stage TX physical address |
| 0x04 | W | Commit RX physical address (1536 writable bytes) |
| 0x08 | W | Commit TX byte length, using staged address |
| 0x0c | R | Four bytes: TX free, RX free, TX done, RX done |
| 0x10 | R | Consume TX DMA completion |
| 0x14 | R | Consume RX completion and return length |
| 0x1c | R | ABI identity `0x4d4e4931` |

Descriptor queues are 16 deep. The polling driver serializes TX and keeps one
RX buffer posted; it fences DMA ownership changes and copies RX before reposting.
RX backpressure prevents the underlying packet buffer from overflowing. TX is
limited to a 1 Gb/s frame-byte rate, not physical wire-rate accounting.

`run_peer_test.sh` checks bridge framing, minimum padding, partial beats and
stable RX data under stalls without compiling Rocket. Host TCP/SSH and FPGA
behavior must not be inferred from that bridge-only test.

## Verified coverage

Validated on the real Rocket Verilator model:

- Kernel C++ startup, PMP-protected U-mode BusyBox, UART commands and HTIF exit 0.
- DMA ARP and ICMP echo, including 512 payload bytes and packet checksums.
- Writable ext4 file creation, readback, rename and deletion.
- Linux TAP TCP connection to guest BusyBox `nc`, remote shell file creation,
  UART readback of `ROCKET_TCP_OK`, and clean simulator shutdown.
- Bridge-only framing, short-frame padding, partial beats, stalls and reset.

The ping acceptance run took about 12 minutes 31 seconds on the development
host. The combined filesystem/TCP run took about 2 hours 3 minutes; background
process image restoration is particularly expensive in this RTL simulation.
These are observed wall times, not performance guarantees. UART logs are
`build/tests/rocket/ping.log` and `build/tests/rocket/tcp.log`.
The optional SSH test has not yet been validated end to end on Rocket.
