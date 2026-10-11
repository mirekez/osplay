#pragma once
#include <devices/registers.h>
#include <osplay/base.hpp>

namespace osplay::extreme_platform {
inline constexpr u32 device_base = EC_BUS_BYTES * EC_BANK_WORDS * EC_BANKS;
inline u32 read(u32 offset) { return *reinterpret_cast<volatile u32 *>(device_base + offset); }
inline void write(u32 offset, u32 value) {
  *reinterpret_cast<volatile u32 *>(device_base + offset) = value;
}
void write_text(const char *text);
[[nodiscard]] bool network_probe();
} // namespace osplay::extreme_platform
