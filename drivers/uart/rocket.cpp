#include <drivers/uart/uart.hpp>

namespace mikos::drivers::uart {
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
