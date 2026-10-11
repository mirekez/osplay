#include <string>

#include <osplay/abi/riscv32.hpp>
#include <osplay/base.hpp>

#include <support/test.hpp>

int main() {
  osplay::test::Suite suite{"kernel/abi_riscv32"};
  using osplay::abi::riscv32::Errno;
  using osplay::abi::riscv32::Syscall;

  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::mkdirat) == 34);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::fchmodat) == 53);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::fchownat) == 54);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::pipe2) == 59);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::write) == 64);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::exit_group) == 94);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::waitid) == 95);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::umask) == 166);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::kill) == 129);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::rt_sigreturn) == 139);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::setsid) == 157);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::brk) == 214);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::clone) == 220);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::execve) == 221);
  OSPLAY_CHECK(suite, static_cast<osplay::i32>(Errno::text_file_busy) == 26);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::wait4) == 260);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::statx) == 291);
  OSPLAY_CHECK(suite,
              static_cast<osplay::u32>(Syscall::clock_gettime64) == 403);
  OSPLAY_CHECK(suite, static_cast<osplay::u32>(Syscall::ppoll64) == 414);
  OSPLAY_CHECK(suite,
              osplay::abi::riscv32::error(Errno::no_syscall) == -38);
  OSPLAY_CHECK(suite,
              std::string{osplay::abi::riscv32::name(64)} == "write");
  OSPLAY_CHECK(suite,
              std::string{osplay::abi::riscv32::name(34)} == "mkdirat");
  OSPLAY_CHECK(suite,
              std::string{osplay::abi::riscv32::name(53)} == "fchmodat");
  OSPLAY_CHECK(
      suite, std::string{osplay::abi::riscv32::name(123)} ==
                 "sched_getaffinity");
  OSPLAY_CHECK(suite,
              std::string{osplay::abi::riscv32::name(166)} == "umask");
  OSPLAY_CHECK(suite,
              std::string{osplay::abi::riscv32::name(0xffffffff)} ==
                  "unknown");

  OSPLAY_CHECK(suite, std::string{osplay::abi::riscv32::name(54)} == "fchownat");

  return suite.finish();
}
