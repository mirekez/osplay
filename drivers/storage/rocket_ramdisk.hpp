#pragma once
#include <osplay/base.hpp>

namespace osplay::drivers::storage {
inline constexpr u64 rocket_ramdisk_base = 0x80000000ULL;
inline constexpr u64 rocket_ramdisk_size = 3ULL << 30;
// Optional preloaded ext2 image. Absence leaves the ordinary rootfs intact.
[[nodiscard]] bool initialize_rocket_ramdisk();
}
