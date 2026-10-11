#include "test_support.hpp"

#include <support/test.hpp>

namespace {

using namespace osplay;
using namespace osplay::drivers::fs;
using namespace osplay::drivers::fs::fat32;

void check_names_and_paths(osplay::test::Suite& suite) {
  fat32::test::Image image;
  constexpr char short_file[11] = {'R', 'E', 'A', 'D', 'M', 'E', ' ',
                                   ' ', 'T', 'X', 'T'};
  constexpr char short_long[11] = {'L', 'O', 'N', 'G', 'N', 'A', 'M',
                                   'E', 'T', 'X', 'T'};
  constexpr char short_dir[11] = {'S', 'U', 'B', 'D', 'I', 'R', ' ',
                                  ' ', ' ', ' ', ' '};
  constexpr char nested[11] = {'I', 'N', 'N', 'E', 'R', ' ', ' ', ' ',
                               'B', 'I', 'N'};
  constexpr char16_t long_name[] = u"Long readable name.txt";

  image.short_entry(2, 0, short_file, 0x20, 4, 3);
  image.set_fat(2, 3);
  image.set_fat(3, 0x0fffffff);
  for (u32 slot = 1; slot < 15; ++slot) {
    image.device.write8(image.cluster_offset(2) + slot * 32, 0xe5);
  }
  image.long_entry(2, 15, 0x40 | 2, image.checksum(short_long),
                   long_name, 22);
  image.long_entry(3, 0, 1, image.checksum(short_long), long_name, 22);
  image.short_entry(3, 1, short_long, 0x20, 5, 8);
  image.short_entry(3, 2, short_dir, 0x10, 6, 0);
  image.end_directory(3, 3);
  image.set_fat(6, 0x0fffffff);
  image.short_entry(6, 0, nested, 0x20, 7, 4);
  image.end_directory(6, 1);

  auto mounted = Volume<fat32::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  auto file = mounted->lookup_path("/readme.txt");
  OSPLAY_CHECK(suite, file);
  OSPLAY_CHECK(suite, file->first_cluster == 4);

  file = mounted->lookup_path("/Long readable name.txt");
  OSPLAY_CHECK(suite, file);
  OSPLAY_CHECK(suite, file->first_cluster == 5);

  file = mounted->lookup_path("SUBDIR/INNER.BIN");
  OSPLAY_CHECK(suite, file);
  OSPLAY_CHECK(suite, file->first_cluster == 7);

  const auto missing = mounted->lookup_path("/missing");
  OSPLAY_CHECK(suite, !missing);
  OSPLAY_CHECK(suite, missing.error() == Error::not_found);
}

void check_orphan_long_name_falls_back(osplay::test::Suite& suite) {
  fat32::test::Image image;
  constexpr char alias[11] = {'A', 'L', 'I', 'A', 'S', ' ', ' ', ' ',
                              'T', 'X', 'T'};
  constexpr char16_t long_name[] = u"Broken long name";
  image.long_entry(2, 0, 0x40 | 2, 0x55, long_name, 16);
  image.long_entry(2, 1, 1, 0x55, long_name, 16);
  image.short_entry(2, 2, alias, 0x20, 4, 0);
  image.end_directory(2, 3);

  auto mounted = Volume<fat32::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  const auto alias_result = mounted->lookup_path("/alias.txt");
  OSPLAY_CHECK(suite, alias_result);
  const auto long_result =
      mounted->lookup_path("/Broken long name");
  OSPLAY_CHECK(suite, !long_result);
}

}  // namespace

int main() {
  osplay::test::Suite suite{"drivers/fs/fat32/directory"};
  check_names_and_paths(suite);
  check_orphan_long_name_falls_back(suite);
  return suite.finish();
}
