#include <osplay/base.hpp>
extern "C" {
alignas(64) __attribute__((section(".tohost"))) volatile osplay::u64 tohost;
alignas(64) __attribute__((section(".tohost"))) volatile osplay::u64 fromhost;
}
namespace osplay {
void rocket_shutdown(u32 code) {
  auto* uart = reinterpret_cast<volatile u32*>(0x10020000);
  uart[2] = 1 | (1 << 16); // TX watermark one: wait for FIFO empty.
  while (!(uart[5] & 1)) {}
  u32 start, now;
  asm volatile("csrr %0, mcycle" : "=r"(start));
  // The shift register can still contain the final byte: 50 MHz / 1 Mbaud.
  do { asm volatile("csrr %0, mcycle" : "=r"(now)); } while (now - start < 1000);
  asm volatile("fence iorw, iorw" ::: "memory");
  // HTIF consumes the low word last; all exit codes fit in 32 bits.
  auto* words = reinterpret_cast<volatile u32*>(&tohost);
  words[1] = 0;
  words[0] = (code << 1) | 1;
  asm volatile("fence iorw, iorw" ::: "memory");
}
}
