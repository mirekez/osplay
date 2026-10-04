#include <__verbose_abort>
#include <mikos/kernel.hpp>
#include <mikos/memory/arena.hpp>
namespace mikos::memory {
[[noreturn]] void allocation_failure() {
  write_text("MIKOS:KERNEL_ALLOCATION_EXHAUSTED\n");
  shutdown(30);
}
[[noreturn]] void allocation_contract_failure() {
  write_text("MIKOS:KERNEL_ALLOCATION_CONTRACT\n");
  shutdown(31);
}
} // namespace mikos::memory
_LIBCPP_BEGIN_NAMESPACE_STD
[[noreturn]] void __libcpp_verbose_abort(const char *, ...) noexcept {
  mikos::memory::allocation_contract_failure();
}
_LIBCPP_END_NAMESPACE_STD
