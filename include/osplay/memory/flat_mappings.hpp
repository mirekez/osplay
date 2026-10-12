#pragma once
#include <osplay/base.hpp>

namespace osplay::memory {

// Flat userspace has no page tables. Track occupied mmap extents so libc can
// release/reuse large buffers; this does not provide per-mapping protection.
// State is copied with process metadata across the snapshot fork adapter.
template <unsigned Capacity = 256>
struct FlatMappings {
  struct Range { uword begin, end; };
  Range ranges[Capacity]{};
  unsigned count{};

  [[nodiscard]] bool unmap(uword address, uword size) {
    if (size == 0) return true;
    if (size > ~uword{0} - address) return false;
    Range kept[Capacity];
    unsigned n = 0;
    const uword end = address + size;
    const auto append = [&](Range range) {
      if (n == Capacity) return false;
      kept[n++] = range;
      return true;
    };
    for (unsigned i = 0; i < count; ++i) {
      const Range r = ranges[i];
      if (r.end <= address || r.begin >= end) {
        if (!append(r)) return false;
      } else {
        if (r.begin < address && !append({r.begin, address})) return false;
        if (r.end > end && !append({end, r.end})) return false;
      }
    }
    for (unsigned i = 0; i < n; ++i) ranges[i] = kept[i];
    count = n;
    return true;
  }

  [[nodiscard]] uword map(uword begin, uword end, uword size,
                         uword address = 0, bool fixed = false) {
    if (begin > end || size == 0 || size > end - begin) return 0;
    if (!fixed) {
      address = begin;
      for (unsigned i = 0; i < count; ++i) {
        if (ranges[i].end <= address) continue;
        if (ranges[i].begin >= address && size <= ranges[i].begin - address) break;
        address = ranges[i].end;
      }
    }
    if (address < begin || address > end || size > end - address) return 0;
    // Insert in address order, coalescing adjacent extents. Allocation IDs are
    // unnecessary: Linux munmap is a range operation, including partial frees.
    // MAP_FIXED overwrites bytes but the occupied extent is simply their union.
    Range kept[Capacity];
    unsigned n = 0;
    bool inserted = false;
    const auto append = [&](Range r) {
      if (n && kept[n - 1].end >= r.begin) {
        if (r.end > kept[n - 1].end) kept[n - 1].end = r.end;
        return true;
      }
      if (n == Capacity) return false;
      kept[n++] = r;
      return true;
    };
    for (unsigned i = 0; i < count; ++i) {
      if (!inserted && ranges[i].begin >= address) {
        if (!append({address, address + size})) return 0;
        inserted = true;
      }
      if (!append(ranges[i])) return 0;
    }
    if (!inserted && !append({address, address + size})) return 0;
    for (unsigned i = 0; i < n; ++i) ranges[i] = kept[i];
    count = n;
    return address;
  }

  [[nodiscard]] uword high_water(uword begin) const {
    return count ? ranges[count - 1].end : begin;
  }
};
}  // namespace osplay::memory
