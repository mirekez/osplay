# C++HDL integration for Extreme

Extreme uses the local `extreme_cpu` symlink for the CPU, compiler, memory
controller and board devices, and `~/cpphdl` for RTL generation. The link points
to the sibling CPU checkout (`../../../extreme_cpu`); scripts resolve it relative
to `tests/extreme`, independently of the caller's working directory.
`prepare_cpphdl.sh` configures those
checkouts without resetting, patching or replacing either one.

The Tribe CPU fixes documented in `tests/tribe/cpphdl-fixes.md` do not apply to
Extreme's ISA or pipeline. In particular, Extreme has no data caches, CSR
interrupt controller or Ethernet DMA engine. Its polling NIC transfers raw
Ethernet frames through CPU-accessible MMIO packet buffers.

The rootless peer protocol is shared with Tribe: one-byte HELLO and length-
prefixed Ethernet datagrams. `net_peer.cpp` independently verifies ARP and a
512-byte ICMP echo, including addresses, checksums and payload. Run the peer
negative test with `make extreme-peer-test`; a corrupted echo must fail.

Device and board RTL regression commands live in `extreme_cpu/devices/README.md`.
Both native C++ and generated SystemVerilog execute the same board testbench.
Kernel commands and the current userspace status are in [README.md](README.md).
