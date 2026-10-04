#include <array>
#include <drivers/net/net.hpp>
#include <drivers/uart/uart.hpp>
#include <kernel/arch/extreme/platform.hpp>
#include <mikos/io/ring_buffer.hpp>
#include <tasks/tasks.h>

namespace mikos {
bool kernel_cxx_smoke();
bool extreme_softfloat_smoke();
} // namespace mikos

namespace mikos::extreme_platform {
// Exercise shared mikOS code on the CPU; this is deliberately distinct from
// kernel_cxx_smoke's full allocator/list/map/unordered_set acceptance marker.
__attribute__((noinline)) bool cxx_probe() {
  io::RingBuffer<8> ring;
  std::array<u8, 8> source{3, 5, 7, 11, 13, 17, 19, 23};
  std::array<u8, 8> result{};
  if (ring.write(source.data(), 8) != 8 || ring.read(result.data(), 3) != 3) {
    return false;
  }
  if (ring.write(source.data(), 3) != 3 || !ring.full()) {
    return false;
  }
  if (ring.read(result.data(), 8) != 8 || !ring.empty()) {
    return false;
  }
  for (u32 i = 0; i < 8; ++i) {
    if (result[i] != source[(i + 3) % 8]) {
      return false;
    }
  }
  return true;
}
void worker() { ::extreme::tasks::finish(cxx_probe() ? 42 : 1); }
bool network_probe() {
  if (!drivers::net::initialize()) {
    return false;
  }
  write_text("MIKOS:NET_IP 192.168.76.2\nMIKOS:NET_MAC 02:00:00:00:00:02\n");
  const MacAddress mac{{2, 0, 0, 0, 0, 2}};
  const Ipv4Address ip{{192, 168, 76, 2}};
  bool arp = false, echo = false;
  for (u32 polls = 0; polls < 1000000; ++polls) {
    drivers::net::Frame frame{};
    if (!drivers::net::receive(frame)) {
      continue;
    }
    u32 size = make_arp_reply(frame.data, frame.size, mac, ip);
    if (size && drivers::net::transmit(frame.data, size)) {
      if (!arp) {
        write_text("MIKOS:ARP_REPLY\n");
      }
      arp = true;
    } else {
      size = make_icmp_echo_reply(frame.data, frame.size, mac, ip);
      if (size && drivers::net::transmit(frame.data, size)) {
        if (!echo) {
          write_text("MIKOS:ICMP_ECHO_REPLY\n");
        }
        echo = true;
      }
    }
    if (arp && echo) {
      return true;
    }
  }
  return false;
}
} // namespace mikos::extreme_platform

extern "C" unsigned kernel() {
  using namespace mikos::extreme_platform;
  if (!mikos::drivers::uart::initialize()) {
    return 1;
  }
  write_text("MIKOS:BOOT\nMIKOS:EXTREME_POLLING\n");
  if (!cxx_probe()) {
    return 2;
  }
  write_text("MIKOS:EXTREME_CXX_OK\nMIKOS:FLAT_DEVICE_IRQ_OFF\n");
#ifndef MIKOS_EXTREME_BOOT_ONLY
  if (!mikos::extreme_softfloat_smoke() || !mikos::kernel_cxx_smoke()) {
    return 6;
  }
  write_text("MIKOS:KERNEL_CXX_OK\n");
#endif
#ifdef MIKOS_EXTREME_MULTICORE
  if (::extreme::tasks::issue(0, worker) != ::extreme::tasks::ok) {
    return 3;
  }
  unsigned result = 0;
  while (::extreme::tasks::read(0, result) == ::extreme::tasks::not_ready) {
  }
  if (result != 42) {
    return 4;
  }
  write_text("MIKOS:EXTREME_TASK_OK\n");
#endif
#ifndef MIKOS_EXTREME_BOOT_ONLY
  if (!network_probe()) {
    return 5;
  }
#endif
  write_text("MIKOS:EXIT 0\n");
  write(::extreme::devices::exit_code, 0);
  return 0;
}
