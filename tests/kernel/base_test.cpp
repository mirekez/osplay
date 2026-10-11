#include <format>
#include <string>

#include <osplay/base.hpp>

#include <support/test.hpp>

int main() {
  osplay::test::Suite suite{"kernel/base"};
  constexpr osplay::UserRange user{0x81000000, 0x84000000};

  OSPLAY_CHECK(suite, user.contains(0x81000000, 1));
  OSPLAY_CHECK(suite, user.contains(0x83ffffff, 1));
  OSPLAY_CHECK(suite, !user.contains(0x84000000, 1));
  OSPLAY_CHECK(suite, !user.contains(0xfffffff0, 0x40));
  OSPLAY_CHECK(suite, user.aligned(0x81000000, 16));
  OSPLAY_CHECK(suite, !user.aligned(0x81000002, 4));
  OSPLAY_CHECK(suite, !user.aligned(0x81000000, 0));
  OSPLAY_CHECK(suite, osplay::align_up(17u, 16u) == 32);
  OSPLAY_CHECK(suite, osplay::align_down(31u, 16u) == 16);
  OSPLAY_CHECK(suite, std::format("{} {}", "C++", 26) == "C++ 26");

  return suite.finish();
}
