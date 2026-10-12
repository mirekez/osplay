#define OSPLAY_RV64
#include <osplay/memory/flat_mappings.hpp>
#include <support/test.hpp>

int main() {
  using namespace osplay;
  test::Suite suite{"kernel/flat_mappings_rv64"};
  const uword begin = uword{8} << 30, end = uword{16} << 30;
  memory::FlatMappings<4> mappings;
  OSPLAY_CHECK(suite, mappings.map(begin, end, 4096) == begin);
  OSPLAY_CHECK(suite, mappings.map(begin, end, 4096) == begin + 4096);
  OSPLAY_CHECK(suite, mappings.count == 1); // Adjacent allocations coalesce.
  OSPLAY_CHECK(suite, mappings.unmap(begin, 4096));
  OSPLAY_CHECK(suite, mappings.map(begin, end, 4096) == begin); // Reuse hole.
  OSPLAY_CHECK(suite, mappings.unmap(begin, 8192));
  OSPLAY_CHECK(suite, mappings.high_water(begin) == begin);
  OSPLAY_CHECK(suite, mappings.map(begin, end, uword{3} << 30) == begin);
  OSPLAY_CHECK(suite, mappings.map(begin, end, 4096, begin + 4096, true) == begin + 4096);
  OSPLAY_CHECK(suite, mappings.count == 1); // MAP_FIXED replaces mapped bytes.
  OSPLAY_CHECK(suite, mappings.unmap(begin + 4096, 4096));
  OSPLAY_CHECK(suite, mappings.count == 2); // Partial free splits an extent.
  const auto snapshot = mappings;
  OSPLAY_CHECK(suite, mappings.map(begin, end, 4096) == begin + 4096);
  mappings = snapshot;
  OSPLAY_CHECK(suite, mappings.map(begin, end, 4096) == begin + 4096);
  OSPLAY_CHECK(suite, !mappings.unmap(~uword{0} - 4, 16));
  OSPLAY_CHECK(suite, mappings.map(begin, end, end - begin + 1) == 0);
  OSPLAY_CHECK(suite, mappings.map(begin, end, 4096, end, true) == 0);
  memory::FlatMappings<1> limited;
  OSPLAY_CHECK(suite, limited.map(begin, end, 12288) == begin);
  OSPLAY_CHECK(suite, limited.map(begin, end, 4096, begin + 4096, true) == begin + 4096);
  OSPLAY_CHECK(suite, limited.unmap(begin + 4096, 0));
  OSPLAY_CHECK(suite, !limited.unmap(begin + 4096, 4096));
  OSPLAY_CHECK(suite, limited.count == 1 && limited.ranges[0].end == begin + 12288);
  OSPLAY_CHECK(suite, limited.map(begin, end, 4096, begin + 16384, true) == 0);
  OSPLAY_CHECK(suite, limited.count == 1 && limited.ranges[0].end == begin + 12288);
  for (unsigned i = 0; i < 1000; ++i) {
    memory::FlatMappings<> loop;
    OSPLAY_CHECK(suite, loop.map(begin, end, uword{3} << 30) == begin);
    OSPLAY_CHECK(suite, loop.unmap(begin, uword{3} << 30));
    OSPLAY_CHECK(suite, loop.map(begin, end, uword{3} << 30) == begin);
  }
  return suite.finish();
}
