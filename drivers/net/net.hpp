#pragma once

#include <osplay/base.hpp>
#include <osplay/net/ethernet.hpp>

namespace osplay::drivers::net {

struct Frame {
  u8* data;
  u32 size;
};

// Polling-only NIC contract. Architecture transports implement this interface;
// Ethernet/IP code does not know whether the device is MMIO or PCI.
[[nodiscard]] bool initialize();
[[nodiscard]] MacAddress mac_address();
[[nodiscard]] bool receive(Frame& frame);
[[nodiscard]] bool transmit(const u8* frame, u32 size);

}  // namespace osplay::drivers::net
