#pragma once
#include <osplay/base.hpp>

namespace osplay::abi::venus {
// OSplay-specific syscall: (physical address, byte count, discard). Cache
// instructions are machine-privileged on Rocket, so userspace cannot issue
// them directly. This is not Linux's instruction-cache flush syscall.
inline constexpr uword cache_range = 0xf01;
inline constexpr u64 dma_begin = 6ULL << 30;
inline constexpr u64 dma_end = 8ULL << 30;
// Keep medlow executable/stack addresses below 2 GiB. Large model allocations
// use a separate RW arena, above both the weights disk and the DMA aperture.
inline constexpr u64 heap_begin = 8ULL << 30;
inline constexpr u64 heap_end = 16ULL << 30;
inline constexpr u64 mmio_begin = 0x20000000;
inline constexpr u64 mmio_size = 0x200000;
}
