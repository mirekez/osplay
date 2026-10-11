#include <__verbose_abort>
#include <osplay/kernel.hpp>
#include <osplay/memory/arena.hpp>
namespace osplay::memory {
[[noreturn]] void allocation_failure() {
  write_text("OSPLAY:KERNEL_ALLOCATION_EXHAUSTED\n");
  shutdown(30);
}
[[noreturn]] void allocation_contract_failure() {
  write_text("OSPLAY:KERNEL_ALLOCATION_CONTRACT\n");
  shutdown(31);
}
} // namespace osplay::memory
_LIBCPP_BEGIN_NAMESPACE_STD
[[noreturn]] void __libcpp_verbose_abort(const char *, ...) noexcept {
  osplay::memory::allocation_contract_failure();
}
_LIBCPP_END_NAMESPACE_STD
