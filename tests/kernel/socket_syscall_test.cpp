#include <osplay/abi/riscv32.hpp>
#include <osplay/abi/socket.hpp>

#include <support/test.hpp>

int main() {
  osplay::test::Suite suite{"kernel/socket_syscall"};
  using osplay::abi::socket::ValidationResult;

  OSPLAY_CHECK(
      suite,
      static_cast<osplay::u32>(osplay::abi::riscv32::Syscall::socket) == 198);
  OSPLAY_CHECK(suite, osplay::abi::socket::validate(2, 2, 0) ==
                         ValidationResult::success);
  OSPLAY_CHECK(suite, osplay::abi::socket::validate(2, 1, 0) ==
                         ValidationResult::success);
  OSPLAY_CHECK(suite, osplay::abi::socket::validate(2, 1, 6) ==
                         ValidationResult::success);
  OSPLAY_CHECK(
      suite,
      osplay::abi::socket::validate(
          2, 2 | osplay::abi::socket::sock_nonblock |
                 osplay::abi::socket::sock_cloexec,
          0) == ValidationResult::success);
  OSPLAY_CHECK(suite, osplay::abi::socket::validate(10, 2, 0) ==
                         ValidationResult::address_family_not_supported);
  OSPLAY_CHECK(suite, osplay::abi::socket::validate(2, 3, 0) ==
                         ValidationResult::socket_type_not_supported);
  OSPLAY_CHECK(suite, osplay::abi::socket::validate(2, 2, 17) ==
                         ValidationResult::protocol_not_supported);
  OSPLAY_CHECK(suite, osplay::abi::socket::validate(2, 1, 17) ==
                         ValidationResult::protocol_not_supported);
  OSPLAY_CHECK(suite, sizeof(osplay::abi::socket::SockaddrIn) == 16);
  OSPLAY_CHECK(
      suite,
      static_cast<osplay::u32>(osplay::abi::riscv32::Syscall::bind) == 200);
  OSPLAY_CHECK(
      suite,
      static_cast<osplay::u32>(osplay::abi::riscv32::Syscall::listen) == 201);
  OSPLAY_CHECK(
      suite,
      static_cast<osplay::u32>(osplay::abi::riscv32::Syscall::accept) == 202);
  OSPLAY_CHECK(
      suite,
      static_cast<osplay::u32>(osplay::abi::riscv32::Syscall::accept4) == 242);
  OSPLAY_CHECK(
      suite,
      static_cast<osplay::u32>(osplay::abi::riscv32::Syscall::pselect6) == 72);
  OSPLAY_CHECK(
      suite,
      static_cast<osplay::u32>(osplay::abi::riscv32::Syscall::ppoll) == 73);
  OSPLAY_CHECK(
      suite,
      static_cast<osplay::u32>(
          osplay::abi::riscv32::Syscall::pselect6_time64) == 413);

  return suite.finish();
}
