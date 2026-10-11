#include <kernel/arch/extreme/platform.hpp>

namespace osplay {
void write_text(const char *text) { extreme_platform::write_text(text); }

// Terminal path for the shared C++ runtime's allocation/assertion failures.
[[noreturn]] void shutdown(u32 code) {
  extreme_platform::write(::extreme::devices::exit_code, code);
  __builtin_trap();
}
} // namespace osplay
