#define OSPLAY_RV64
#include <algorithm>
#include <cstring>
#include <support/test.hpp>
#include <sys/mman.h>
#include <kernel/syscall.cpp>

namespace osplay {
Process process{};
}

int main() {
  using namespace osplay;
  test::Suite suite{"kernel/brk_zero_growth"};
  constexpr uword base = user_begin;
  constexpr usize bytes = 16384;
  auto* memory = static_cast<u8*>(::mmap(reinterpret_cast<void*>(base), bytes,
      PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0));
  OSPLAY_CHECK(suite, memory == reinterpret_cast<u8*>(base));
  if (memory != reinterpret_cast<u8*>(base)) return suite.finish();
  std::memset(memory, 0xa5, bytes);
  process.brk = base + 512;
  process.mmap_cursor = base + bytes;
  OSPLAY_CHECK(suite, program_break(0) == static_cast<iword>(base + 512));
  OSPLAY_CHECK(suite, program_break(base + 4096) == static_cast<iword>(base + 4096));
  OSPLAY_CHECK(suite, memory[511] == 0xa5);
  OSPLAY_CHECK(suite, std::all_of(memory + 512, memory + 4096, [](u8 byte) { return byte == 0; }));
  OSPLAY_CHECK(suite, memory[4096] == 0xa5);

  // Re-exposing a released heap interval must not leak its old contents.
  OSPLAY_CHECK(suite, program_break(base + 2048) == static_cast<iword>(base + 2048));
  std::memset(memory + 2048, 0x7e, 2048);
  memory[2047] = 0x3d;
  OSPLAY_CHECK(suite, program_break(base + 4096) == static_cast<iword>(base + 4096));
  OSPLAY_CHECK(suite, memory[2047] == 0x3d);
  OSPLAY_CHECK(suite, std::all_of(memory + 2048, memory + 4096, [](u8 byte) { return byte == 0; }));
  OSPLAY_CHECK(suite, program_break(base + bytes) == static_cast<iword>(base + 4096));
  OSPLAY_CHECK(suite, memory[4096] == 0xa5);
  OSPLAY_CHECK(suite, ::munmap(memory, bytes) == 0);
  return suite.finish();
}
