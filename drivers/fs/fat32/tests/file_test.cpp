#include "test_support.hpp"

#include <support/test.hpp>

namespace {

using namespace osplay;
using namespace osplay::drivers::fs;
using namespace osplay::drivers::fs::fat32;

void check_lookup_and_read(osplay::test::Suite& suite) {
  fat32::test::Image image;
  constexpr char name[11] = {'P', 'A', 'Y', 'L', 'O', 'A', 'D', ' ',
                             'B', 'I', 'N'};
  image.short_entry(2, 0, name, 0x20, 4, 700);
  image.end_directory(2, 1);
  image.set_fat(4, 8);
  image.set_fat(8, 0x0fffffff);
  u8 first[512]{};
  u8 second[188]{};
  for (u32 index = 0; index < sizeof(first); ++index) {
    first[index] = static_cast<u8>(index * 3);
  }
  for (u32 index = 0; index < sizeof(second); ++index) {
    second[index] = static_cast<u8>(0xf0 - (index & 0x7f));
  }
  image.write_cluster(4, 0, first, sizeof(first));
  image.write_cluster(8, 0, second, sizeof(second));

  auto mounted = Volume<fat32::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  const auto file = mounted->lookup_path("/payload.bin");
  OSPLAY_CHECK(suite, file);
  u8 output[256]{};
  const auto read = mounted->read((*file), 500, output,
                                       sizeof(output));
  OSPLAY_CHECK(suite, read);
  OSPLAY_CHECK(suite, (*read) == 200);
  for (u32 index = 0; index < 12; ++index) {
    OSPLAY_CHECK(suite, output[index] == first[500 + index]);
  }
  for (u32 index = 12; index < 200; ++index) {
    OSPLAY_CHECK(suite, output[index] == second[index - 12]);
  }
  const auto eof = mounted->read((*file), 700, output, 1);
  OSPLAY_CHECK(suite, eof);
  OSPLAY_CHECK(suite, (*eof) == 0);

  const auto directory_read =
      mounted->read(mounted->root(), 0, output, 1);
  OSPLAY_CHECK(suite, !directory_read);
}

}  // namespace

int main() {
  osplay::test::Suite suite{"drivers/fs/fat32/file"};
  check_lookup_and_read(suite);
  return suite.finish();
}
