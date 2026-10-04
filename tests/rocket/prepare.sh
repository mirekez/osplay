#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/paths.sh"
mode="${1:-all}"
[[ "$mode" == all || "$mode" == --sim-only || "$mode" == --kernel-only ]] || {
  echo "usage: $0 [--sim-only|--kernel-only]" >&2; exit 2;
}
if [[ "$mode" != --sim-only ]]; then
  make -C "$root/tests/busybox" rootfs LLVM_READELF="$readelf" JOBS="${ROCKET_JOBS:-2}"
  make -C "$root" -j"${ROCKET_JOBS:-2}" rocket-kernel CXX="$host_clang" LLVM_READELF="$readelf"
fi
[[ "$mode" != --kernel-only ]] || exit 0
[[ -f "$chipyard/generators/chipyard/src/main/scala/example/EthernetPCIe.scala" ]] || {
  echo "Apply Chipyard ethernet.patch first (EthernetPCIe.scala is required)." >&2; exit 1;
}
install_if_changed() { cmp -s "$1" "$2" || install -m644 "$1" "$2"; }
install_if_changed "$root/tests/rocket/MikosRocket.scala" \
  "$chipyard/generators/chipyard/src/main/scala/config/MikosRocket.scala"
install_if_changed "$root/tests/rocket/MikosNetBridge.sv" \
  "$chipyard/generators/chipyard/src/main/resources/vsrc/MikosNetBridge.sv"
export RISCV="$envdir/riscv-tools"
export PATH="$RISCV/bin:$envdir/bin:/usr/bin:/bin"
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:--Xmx2G -Xss8M}"
export CHIPYARD_DISABLE_OPTIONAL_MODULES=1
unset VERILATOR_ROOT
cxx="$envdir/bin/x86_64-conda-linux-gnu-c++"
make -C "$chipyard/sims/verilator" CONFIG=MikosRocketConfig \
  VERILATOR="$envdir/bin/verilator --main --timing --cc --exe" \
  VERILATOR_THREADS=1 CLOCK_PERIOD=20.0 CXX="$cxx" LINK="$cxx" \
  EXTRA_SIM_CXXFLAGS=-std=c++20 \
  EXTRA_SIM_SOURCES="$root/tests/rocket/net_bridge.cpp" \
  EXTRA_SIM_REQS="$root/tests/rocket/net_bridge.cpp $root/tests/rocket/MikosNetBridge.sv $root/tests/rocket/prepare.sh" \
  EXTRA_SIM_LDFLAGS='-static-libstdc++ -static-libgcc -no-pie -Wl,--allow-multiple-definition -Wl,--start-group -lriscv -lfdt -lsoftfloat -ldisasm -Wl,--end-group -ldl' \
  -j"${ROCKET_JOBS:-2}"
