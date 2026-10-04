#include <drivers/uart/uart.hpp>
#include <kernel/arch/extreme/platform.hpp>

namespace mikos::drivers::uart {
bool initialize() {
  return extreme_platform::read(::extreme::devices::identity) == ::extreme::devices::device_id;
}
void put(u8 value) {
  while ((extreme_platform::read(::extreme::devices::uart_status) & 1) == 0) {
  }
  extreme_platform::write(::extreme::devices::uart_tx, value);
}
bool ready() { return (extreme_platform::read(::extreme::devices::uart_status) & 2) != 0; }
bool receive(u8 &value) {
  if (!ready()) {
    return false;
  }
  value = static_cast<u8>(extreme_platform::read(::extreme::devices::uart_rx));
  return true;
}
} // namespace mikos::drivers::uart

namespace mikos::extreme_platform {
void write_text(const char *text) {
  while (*text) {
    drivers::uart::put(static_cast<u8>(*text++));
  }
}
} // namespace mikos::extreme_platform
