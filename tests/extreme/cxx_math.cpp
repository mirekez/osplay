#include <bit>
#include <osplay/base.hpp>

extern "C" float ceilf(float) noexcept;

namespace osplay {
// Exercise the compiler-rt operations needed by the actual unordered_set test.
// Expected IEEE-754 encodings include subnormals, signed zero, infinity and NaN.
bool extreme_softfloat_smoke() {
  struct Case {
    u32 a, b, product, quotient, comparison;
  };
  static volatile Case cases[] = {{0x40c00000, 0x40400000, 0x41900000, 0x40000000, 50},
                                  {0xc0c00000, 0x40400000, 0xc1900000, 0xc0000000, 14},
                                  {0x00800000, 0x3f000000, 0x00400000, 0x01000000, 14},
                                  {0x00000000, 0xc0000000, 0x80000000, 0x80000000, 50},
                                  {0x7f800000, 0x40000000, 0x7f800000, 0x7f800000, 50},
                                  {0x7fc00000, 0x3f800000, 0x7fc00000, 0x7fc00000, 66},
                                  {0x80000000, 0x00000000, 0x80000000, 0x7fc00000, 41}};
  for (unsigned i = 0; i < 7; ++i) {
    float a = std::bit_cast<float>(u32(cases[i].a));
    float b = std::bit_cast<float>(u32(cases[i].b));
    unsigned comparison = (a == b) | ((a != b) << 1) | ((a < b) << 2) | ((a <= b) << 3) |
                          ((a > b) << 4) | ((a >= b) << 5) | (__builtin_isunordered(a, b) << 6);
    if (comparison != cases[i].comparison || std::bit_cast<u32>(a * b) != cases[i].product ||
        std::bit_cast<u32>(a / b) != cases[i].quotient) {
      return false;
    }
  }
  static volatile u32 integers[] = {0, 1, 0x1000000, 0x80000000};
  static const u32 encodings[] = {0, 0x3f800000, 0x4b800000, 0x4f000000};
  for (unsigned i = 0; i < 4; ++i) {
    u32 integer = integers[i];
    float value = float(integer);
    if (std::bit_cast<u32>(value) != encodings[i] || u32(value) != integer) {
      return false;
    }
  }
  static volatile u32 ceilings[] = {0xbfc00000, 0x3fc00000, 0x80000000};
  static const u32 rounded[] = {0xbf800000, 0x40000000, 0x80000000};
  for (unsigned i = 0; i < 3; ++i) {
    if (std::bit_cast<u32>(ceilf(std::bit_cast<float>(u32(ceilings[i])))) != rounded[i]) {
      return false;
    }
  }
  return true;
}
} // namespace osplay
