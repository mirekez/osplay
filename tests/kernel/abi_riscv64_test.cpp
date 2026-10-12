#define OSPLAY_RV64
#include <osplay/arch.hpp>
#include <osplay/scheduler.hpp>
#include <osplay/abi/iovec.hpp>
#include <osplay/io/scatter_write.hpp>
#include <support/test.hpp>
#include <array>

static_assert(sizeof(osplay::uword) == 8);
static_assert(sizeof(osplay::TrapFrame) == 560);
static_assert(__builtin_offsetof(osplay::TrapFrame, mepc) == 256);
static_assert(__builtin_offsetof(osplay::TrapFrame, fp) == 288);
static_assert(__builtin_offsetof(osplay::TrapFrame, fcsr) == 544);
static_assert(osplay::arch::scheduler_timer_cause == (osplay::u64{1} << 63 | 7));
static_assert(sizeof(osplay::abi::Iovec64) == 16);
static_assert(osplay::align_up(osplay::u64{0x100000012}, osplay::u32{16}) == 0x100000020);
static_assert(osplay::align_down(osplay::u64{0x100000012}, osplay::u32{16}) == 0x100000010);

int main() {
  using namespace osplay;
  test::Suite suite{"kernel/abi_riscv64"};
  const UserRange memory{0x41000000, 0x42000000};
  OSPLAY_CHECK(suite, memory.contains(0x41000000, 8));
  OSPLAY_CHECK(suite, !memory.contains(0x141000000, 8));
  OSPLAY_CHECK(suite, !memory.contains(0x41ffffff, 2));
  const std::array vectors{abi::Iovec64{0x141000000, 4}, abi::Iovec64{0x241000000, 6}};
  unsigned calls = 0;
  auto writer = [&](u64 base, u64 size) -> i32 {
    OSPLAY_CHECK(suite, base == (calls == 0 ? 0x141000000 : 0x241000000));
    ++calls;
    return static_cast<i32>(size);
  };
  OSPLAY_CHECK(suite, io::scatter_write64(vectors, writer, -22) == 10 && calls == 2);
  const std::array overflow{abi::Iovec64{0, u64{1} << 32}};
  OSPLAY_CHECK(suite, io::scatter_write64(overflow, writer, -22) == -22 && calls == 2);
  return suite.finish();
}
