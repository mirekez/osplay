#include <drivers/net/net.hpp>

namespace osplay::drivers::net {
namespace {
volatile u64* const regs64 = reinterpret_cast<volatile u64*>(0x10016000);
volatile u32* const counts = reinterpret_cast<volatile u32*>(0x10016014);
volatile u16* const completed_rx = reinterpret_cast<volatile u16*>(0x10016012);
volatile u8* const completed_tx = reinterpret_cast<volatile u8*>(0x10016010);
alignas(64) u8 rx[1536];
alignas(64) u8 tx[1536];
alignas(64) u8 delivered[1536];
void fence() { asm volatile("fence iorw, iorw" ::: "memory"); }
void post_rx() { fence(); regs64[1] = reinterpret_cast<usize>(rx); fence(); }
}
bool initialize() {
  // Native IceNIC RV64 ABI, not the RV32 OSplay address-staging shim.
  if (regs64[3] != 0x020000000002ULL) return false;
  *reinterpret_cast<volatile u8*>(0x10016020) = 0;
  post_rx();
  return true;
}
MacAddress mac_address() { return {{2, 0, 0, 0, 0, 2}}; }
bool receive(Frame& frame) {
  if (!(*counts & 0xff000000u)) return false;
  const u32 length = *completed_rx;
  fence();
  if (length > sizeof(rx)) { post_rx(); return false; }
  for (u32 i=0; i<length; ++i) delivered[i] = rx[i];
  post_rx();
  frame = {delivered, length};
  return true;
}
bool transmit(const u8* frame, u32 length) {
  if (length < 14 || length > 1514) return false;
  const u32 padded = length < 60 ? 60 : length;
  for (u32 i=0; i<padded; ++i) tx[i] = i<length ? frame[i] : 0;
  fence();
  while (!(*counts & 0xff)) {}
  regs64[0] = reinterpret_cast<usize>(tx) | (u64(padded) << 48);
  fence();
  while (!(*counts & 0xff0000u)) {}
  (void)*completed_tx;
  fence();
  return true;
}
}
