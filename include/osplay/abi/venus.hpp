#pragma once
#include <osplay/base.hpp>

namespace osplay::abi::venus {
// OSplay-specific syscall: (physical address, byte count, discard). Cache
// instructions are machine-privileged on Rocket, so userspace cannot issue
// them directly. This is not Linux's instruction-cache flush syscall.
inline constexpr uword cache_range = 0xf01;
// Flush only the 64-byte rows at address+row*stride: (address, rows, stride).
// Batches a strided DMA tile into one privilege transition, without flushing
// unrelated cache lines between its rows. It never invalidates dirty data.
inline constexpr uword cache_rows = 0xf03;
inline constexpr u64 dma_begin = 6ULL << 30;
inline constexpr u64 dma_end = 8ULL << 30;
// Keep medlow executable/stack addresses below 2 GiB and put large allocations
// in a separate RW arena beyond the model weights.
#ifdef OSPLAY_VENUS_HEAP_BEGIN
inline constexpr u64 heap_begin = OSPLAY_VENUS_HEAP_BEGIN;
#elif defined(OSPLAY_VENUS_ARCHIVE)
inline constexpr u64 heap_begin = 0x160000000ULL;
#else
inline constexpr u64 heap_begin = 8ULL << 30;
#endif
#ifdef OSPLAY_VENUS_HEAP_END
inline constexpr u64 heap_end = OSPLAY_VENUS_HEAP_END;
#elif defined(OSPLAY_VENUS_ARCHIVE)
inline constexpr u64 heap_end = 6ULL << 30;
#else
inline constexpr u64 heap_end = 16ULL << 30;
#endif
static_assert(heap_end > heap_begin && ((heap_begin | heap_end) & 0xfff) == 0);
static_assert(heap_end <= dma_begin || heap_begin >= dma_end);
#ifdef OSPLAY_VENUS_ARCHIVE
inline constexpr uword archive_memory = 0xf04;
inline constexpr u64 archive_begin = 3ULL << 30;
inline constexpr u64 archive_end = heap_begin;
inline constexpr u64 user_arena_begin = archive_begin;
static_assert(archive_begin < archive_end && (archive_begin & 0xfff) == 0);
static_assert(heap_end <= dma_begin || archive_begin >= dma_end);
#else
inline constexpr u64 user_arena_begin = heap_begin;
#endif
// Optional bootloader-initialized storage, not anonymous mmap memory. Enabled
// only with OSPLAY_PRELOADED_MEMORY; ordinary allocations stop before it.
inline constexpr uword initialized_memory = 0xf02;
inline constexpr u64 initialized_begin = 13ULL << 30;
inline constexpr u64 initialized_end = heap_end;
inline constexpr u64 initialized_header_bytes = 4096;
#ifdef OSPLAY_PRELOADED_MEMORY
static_assert(initialized_begin + initialized_header_bytes <= initialized_end);
#endif
struct InitializedMemory {
  u64 magic, bytes, metadata_bytes, payload_offset;
  [[nodiscard]] constexpr bool valid() const {
    return magic == 0x3130304d454d504fULL &&
        payload_offset == initialized_header_bytes && bytes != 0 &&
        bytes <= initialized_end - initialized_begin - payload_offset &&
        metadata_bytes <= initialized_header_bytes - sizeof(InitializedMemory);
  }
};
inline constexpr u64 mmio_begin = 0x20000000;
inline constexpr u64 mmio_size = 0x200000;
}
