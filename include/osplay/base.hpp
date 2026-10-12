#pragma once

namespace osplay {

using u8 = __UINT8_TYPE__;
using u16 = __UINT16_TYPE__;
using u32 = __UINT32_TYPE__;
using u64 = __UINT64_TYPE__;
using i32 = __INT32_TYPE__;
using usize = __SIZE_TYPE__;
#ifdef OSPLAY_RV64
using uword = u64;
using iword = __INT64_TYPE__;
#else
using uword = u32;
using iword = i32;
#endif

struct UserRange {
  uword begin;
  uword end;

  [[nodiscard]] constexpr bool contains(uword address, uword size) const {
    return address >= begin && address <= end && size <= end - address;
  }

  [[nodiscard]] constexpr bool aligned(uword address, uword alignment) const {
    return alignment != 0 && (alignment & (alignment - 1)) == 0 &&
           (address & (alignment - 1)) == 0;
  }
};

template <typename T, typename A>
[[nodiscard]] constexpr T align_up(T value, A alignment) {
  const T mask = static_cast<T>(alignment) - 1;
  return (value + mask) & ~mask;
}

template <typename T, typename A>
[[nodiscard]] constexpr T align_down(T value, A alignment) {
  const T mask = static_cast<T>(alignment) - 1;
  return value & ~mask;
}

}  // namespace osplay
