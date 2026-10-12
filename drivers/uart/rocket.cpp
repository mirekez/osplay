#include <drivers/uart/uart.hpp>
#ifdef OSPLAY_HTIF_UART_TRACE
extern "C" volatile osplay::u64 tohost;
#endif

namespace osplay::drivers::uart {
namespace {
volatile u32* const uart = reinterpret_cast<volatile u32*>(0x10020000);
bool pending = false;
u8 saved;
}
bool initialize() {
  uart[2] = 1; // TX enable, one stop bit.
  uart[3] = 1; // RX enable.
  uart[4] = 0; // Polling: mask interrupts.
  return true;
}
void put(u8 value) {
#ifdef OSPLAY_HTIF_UART_TRACE
  // Diagnostic only: a separate channel when investigating the physical UART.
  // Acceptance tests must still receive the real UART bytes through SimUART.
  while (tohost != 0) {}
  tohost = (u64{1} << 56) | (u64{1} << 48) | value;
  asm volatile("fence iorw, iorw" ::: "memory");
  // FESVR's console write consumes tohost but does not return a response.
  while (tohost != 0) {}
#endif
  while (uart[0] & 0x80000000u) {}
  uart[0] = value;
}
bool ready() {
  if (!pending) {
    const u32 data = uart[1]; // Reading rxdata consumes one byte.
    if (!(data & 0x80000000u)) { saved = data; pending = true; }
  }
  return pending;
}
bool receive(u8& value) {
  if (!ready()) return false;
  value = saved;
  pending = false;
  return true;
}
}
