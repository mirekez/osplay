# OSplay on Rocket

The original scripts in this directory run real Chipyard **RV32 Rocket RTL in
Verilator**, not an ISA emulator. A separate RV64 kernel/userspace build is
available for Venus's native-graph integration, described below. The RV32
scripts and their hardware configuration are unchanged.

## RV64 Venus integration

From a Venus checkout containing this OSplay tree:

```bash
bash deploy/run_osplay_mlaccel.sh --build-only
bash deploy/run_osplay_mlaccel.sh --boot-test
bash deploy/run_osplay_mlaccel.sh --test
```

The launcher uses `VenusOSplayRocketConfig`: one RV64 Rocket, two DRAM channels,
Venus, SIMD, RoCC, and the standard coherent IceNIC DMA engine. The complete
hardware design runs in cpphdl native graph. UART, raw Ethernet frames and
external DRAM are host boundary services, not device emulations replacing DMA.
The NIC TX limiter permits five 64-bit flits per sixteen 50 MHz clocks: a
1 Gbit/s frame-byte rate, excluding Ethernet PHY/FCS/inter-frame-gap overhead.

`rocket64-kernel` builds `build/osplay-rocket-rv64.elf`, using separate
`build/rocket64` objects. Userspace lives in `build/tests/busybox-rv64` and
requires a static RV64 Linux toolchain. This flat LP64D profile maps 16 GiB
DRAM at `0x40000000`, BusyBox at `0x41000000`, Dropbear at `0x41400000`, HTIF
at `0x43000000`, and the ext4 RAM disk at `0x44000000`. These low addresses
support the toolchain's medlow glibc. Trap handling preserves all FP registers.
M-mode kernel/U-mode userspace still use PMP with virtual translation disabled.

Select `rocket-kernel` for RV32/ILP32 or `rocket64-kernel` for RV64/LP64D.
Their objects and userspace root filesystems are separate. `OSPLAY_RV64` is
set only by the RV64 target: address-sized `uword` fields stay 32-bit in RV32.
RV64 syscall 62 uses `lseek(fd, offset, whence)` and returns the file position;
RV32 retains its five-argument `llseek` ABI. RV64 also uses the LP64 `sysinfo`
layout and bounded mmap extent tracking to reuse released large allocations.
Neither mode gains virtual translation or per-mapping memory protection.

`RISCV_PREFIX`/`RISCV_SYSROOT` select the RV32 kernel headers/binutils.
`RISCV64_PREFIX`/`RISCV64_SYSROOT` independently select the RV64 kernel tools;
set the prefix to a static RV64 Linux toolchain when using the Venus launcher.
For example, both configurations can coexist without changing the RV32 prefix:

```bash
make rocket-kernel RISCV_PREFIX=/path/to/rv32/bin/riscv32-unknown-elf-
make rocket64-kernel RISCV64_PREFIX=/path/to/rv64/bin/riscv64-unknown-linux-gnu-
```

`OSPLAY_VENUS` is an optional RV64 deployment profile, not a global OSplay
architecture switch. It grants a 2 GiB DMA aperture at 6 GiB and an 8 GiB
read/write model mmap arena at 8 GiB; executable/stack addresses remain low.
The kernel, HTIF, rootfs and weights disk remain outside U-mode PMP access.
The RV32 profile does not enable these high-memory apertures.

No sudo is required for the default network mode. The new
[rootless Ethernet proxy](../network/README.md) forwards host
`127.0.0.1:2222` to guest `192.168.76.2:22`; it does not bypass the NIC or guest
TCP/IP stack. `--tap` selects the existing TAP helper instead. Interactive mode
prints the exact `dbclient` command; `--test` waits for Dropbear, runs shell/file
commands over SSH, then requests a clean UART/HTIF shutdown. `--boot-test`
independently checks DMA ARP/ICMP replies and UART shell commands.

The Venus deployment can preload a separate 3 GiB ext2 weights image at
`0x80000000`, mounted read-only at `/weights`, using
`bash deploy/run_osplay_mlaccel.sh --weights DIR`. The two DRAM channels share
an interleaved physical address space; this reserves an address range, not a
whole independently addressed channel. Host image loading happens before
reset; file reads still execute on Rocket through the ordinary filesystem.
`--disk-test` uses a focused shell without Dropbear startup, verifies all file
sizes and four byte ranges per file in the kernel, checks U-mode file access
and `/proc/mounts`, and requires exit 0. Its default source is
`~/harpy/cpuml/Llama3.2_1B-Instruct/weights_fp16`. The ordinary small rootfs and
all RV32 profiles are unchanged. The backing bytes must outlive their memory
mount; `root::mount_memory` does not copy or own them.

Logs are saved by the Venus launcher. Build and unit-test success alone do not
establish that guest SSH works; the full `--test` must pass for that claim.

## Build and test

```bash
cd ~/osplay
export OSPLAY_CXX=~/cpphdl/.conda/bin/clang++
tests/rocket/prepare.sh
tests/rocket/run_all.sh
```

All scripts use the `chipyard` symlink beside them in `tests/rocket/`, regardless
of the shell's working directory. It points to `/home/me/chipyard/chipyard` in
this workspace. To select another checkout, update that symlink;
`CHIPYARD_HOME` is no longer used.

Prerequisites: an initialized Chipyard checkout with `ethernet.patch` installed,
its `.conda-env` (Verilator with timing support, host GCC, firtool and FESVR),
the RV32 Linux toolchain described in the main README, and the existing OSplay
kernel-C++ dependencies. `prepare.sh` installs only `OSplayRocket.scala` and
`OSplayNetBridge.sv` into Chipyard, then builds the kernel/rootfs and simulator.
It does not run Chipyard's destructive environment/bootstrap setup script.
`--kernel-only` and `--sim-only` build either side separately. `ROCKET_JOBS`
defaults to two to bound compilation memory. `OSPLAY_READELF`, `CHIPYARD_ENV`,
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

## File-backed virtual disks

OSplay now exposes `/dev/loop-control` and four `/dev/loopN` devices. A loop
device keeps an existing regular file as its disk; it does not copy the disk
image into a second RAM allocation. Rocket's root disk is already RAM-backed,
so these images and their mounted contents are private to the running guest.

Create, format and mount one from the guest console:

```sh
dd if=/dev/zero of=/disk.img bs=1024 count=256
mkfs.ext2 -F /disk.img
mkdir /disk
mount -t ext2 -o loop /disk.img /disk
echo hello >/disk/hello
cat /disk/hello
umount /disk
mount -t ext2 -o loop,ro /disk.img /disk
cat /disk/hello
umount /disk
```

The same mount path accepts supported ext4 images. Writable ext4 images must
be journal-free and without metadata checksums, matching the rootfs driver.
Loop offsets and size limits are supported in 512-byte units; other filesystem
types, loop encryption/direct I/O, bind/remount/lazy unmount, and backing files
on another loop mount are explicitly unsupported. Attached backing files are
pinned against write/truncate/unlink/rename until detached. Ordinary unmount
returns `EBUSY` for open mounted files, working directories, and child mounts.
`mount -o loop` enables autoclear, so successful unmount releases its device
once the last loop descriptor closes. `losetup` attachments remain until
`losetup -d /dev/loopN`.
New ext2 files support direct and single-indirect blocks; double/triple-indirect
mutations are rejected before removing directory entries or changing inodes.

The special regression runs on real Rocket RTL, creates/formats its image in
the guest, checks writes/rename, open-fd and cwd busy failures, read-only
remount, persistence, repeated mounting, removal, and clean HTIF shutdown:

```sh
tests/rocket/.test_ramdisk.sh --build  # build simulator/kernel, then test
tests/rocket/.test_ramdisk.sh          # reuse existing build
make rocket-ramdisk-test              # rebuild kernel, then test
```

It needs no host loop device, root mount or sudo. UART output is saved to
`build/tests/rocket/ramdisk.log`; `ROCKET_TIMEOUT` sets its wall-time limit
(default 7200 seconds), and `ROCKET_CYCLES` sets the simulator cycle limit.
This runner rebuilds a dedicated `osplay-rocket-ramdisk-rv32.elf` with the same
kernel and drivers but a non-login interactive shell: SSH startup is skipped
only for this regression. Its 64 KiB image keeps RTL execution bounded. The
normal Rocket console still boots its usual login shell and Dropbear server.

## Interactive Ethernet setup

```bash
bash tests/network/build_tap.sh
sudo bash tests/network/start_tap.sh --background
tests/rocket/rocket_interactive.sh --no-build
```

The default bridge is shared with Tribe and Extreme: `tap-osplay`, socket
`/tmp/osplay-tap.sock`. See [shared TAP setup](../network/TAP.md) for reusing
an existing bridge via `OSPLAY_TAP` and `OSPLAY_TAP_SOCKET`. Only one simulator
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
starts Dropbear automatically. Wait for `OSPLAY:TCP_LISTEN 22` before connecting
from the host; no console setup command is needed. Starting the background
server entails a full BusyBox image restoration and can delay the first shell
prompt by several minutes in RTL simulation. Type `exit` at the console to stop the guest. UART stdin is
not redirected away.
The early `OSPLAY:NET_IP 10.0.2.15` banner precedes interactive interface
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
  virtual-memory translation is required by this OSplay port.
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
