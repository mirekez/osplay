#pragma once

#include <osplay/base.hpp>

namespace osplay::drivers::uart {

[[nodiscard]] bool initialize();
void put(u8 value);
[[nodiscard]] bool ready();
[[nodiscard]] bool receive(u8& value);

}  // namespace osplay::drivers::uart
