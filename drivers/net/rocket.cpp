#include <drivers/net/net.hpp>

namespace osplay::drivers::net {
namespace {
volatile u32* const regs = reinterpret_cast<volatile u32*>(0x10016000);
alignas(64) u8 rx[1536];
alignas(64) u8 tx[1536];
alignas(64) u8 delivered[1536];
inline void fence() { asm volatile("fence iorw, iorw" ::: "memory"); }
void post_rx() { fence(); regs[1] = reinterpret_cast<u32>(rx); fence(); }
}
bool initialize() {
  if (regs[7] != 0x4d4e4931) return false; // MNI1: staged RV32 descriptor ABI.
  post_rx();
  return true;
}
MacAddress mac_address() { return {{2, 0, 0, 0, 0, 2}}; }
bool receive(Frame& frame) {
  if (!(regs[3] & 0xff000000u)) return false;
  const u32 length = regs[5];
  fence();
  if (length > sizeof(rx)) { post_rx(); return false; }
  for (u32 i = 0; i < length; ++i) delivered[i] = rx[i];
  post_rx();
  frame = {delivered, length};
  return true;
}
bool transmit(const u8* frame, u32 length) {
  if (length < 14 || length > 1514) return false;
  const u32 padded = length < 60 ? 60 : length;
  for (u32 i = 0; i < padded; ++i) tx[i] = i < length ? frame[i] : 0;
  fence();
  while (!(regs[3] & 0xff)) {}
  regs[0] = reinterpret_cast<u32>(tx);
  regs[2] = padded; // Commit TX after address and data are visible.
  fence();
  while (!(regs[3] & 0xff0000u)) {}
  (void)regs[4]; // Consume DMA completion before reusing tx.
  fence();
  return true;
}
}
