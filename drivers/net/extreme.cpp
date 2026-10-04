#include <drivers/net/net.hpp>
#include <kernel/arch/extreme/platform.hpp>

namespace mikos::drivers::net {
namespace {
alignas(64) u8 packet[::extreme::devices::packet_bytes];
}
bool initialize() {
  return extreme_platform::read(::extreme::devices::identity) == ::extreme::devices::device_id;
}
MacAddress mac_address() { return {{2, 0, 0, 0, 0, 2}}; }
bool receive(Frame &frame) {
  const u32 size = extreme_platform::read(::extreme::devices::rx_length);
  if (size == 0) {
    return false;
  }
  if (size > sizeof(packet)) {
    extreme_platform::write(::extreme::devices::rx_release, 1);
    return false;
  }
  // Packet windows have no read side effects. CPU reads the frame; there is no DMA.
  const auto *source = reinterpret_cast<volatile const u32 *>(extreme_platform::device_base +
                                                              ::extreme::devices::rx_buffer);
  auto *destination = reinterpret_cast<u32 *>(packet);
  for (u32 i = 0; i < (size + 3) / 4; ++i) {
    destination[i] = source[i];
  }
  extreme_platform::write(::extreme::devices::rx_release, 1);
  frame = {packet, size};
  return true;
}
bool transmit(const u8 *frame, u32 size) {
  if (size == 0 || size > sizeof(packet)) {
    return false;
  }
  while ((extreme_platform::read(::extreme::devices::net_status) & 1) == 0) {
  }
  auto *destination = reinterpret_cast<volatile u32 *>(extreme_platform::device_base +
                                                       ::extreme::devices::tx_buffer);
  for (u32 offset = 0; offset < size; offset += 4) {
    u32 value = 0;
    for (u32 byte = 0; byte < 4 && offset + byte < size; ++byte) {
      value |= u32(frame[offset + byte]) << (byte * 8);
    }
    destination[offset / 4] = value;
  }
  extreme_platform::write(::extreme::devices::tx_length, size);
  extreme_platform::write(::extreme::devices::tx_submit, 1);
  // Completion means the MAC-side stream has accepted the whole frame.
  while ((extreme_platform::read(::extreme::devices::net_status) & 1) == 0) {
  }
  return true;
}
} // namespace mikos::drivers::net
