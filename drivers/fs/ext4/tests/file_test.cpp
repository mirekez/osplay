#include "test_support.hpp"

#include <support/test.hpp>

namespace {

using namespace osplay;
using namespace osplay::drivers::fs;
using namespace osplay::drivers::fs::ext4;

void check_sparse_partial_read(osplay::test::Suite& suite) {
  ext4::test::Image image;
  image.two_extent_inode(3, 0x81a4, 3 * 1024, 0, 11, 1, 2, 12, 1);
  u8 first[1024]{};
  u8 third[1024]{};
  for (u32 index = 0; index < 1024; ++index) {
    first[index] = 0x11;
    third[index] = 0x33;
  }
  image.write_block(11, first, sizeof(first));
  image.write_block(12, third, sizeof(third));

  auto mounted = Volume<ext4::test::Device>::mount(image.device);
  const auto inode = mounted->read_inode(3);
  u8 output[1200]{};
  const auto read =
      mounted->read((*inode), 900, output, sizeof(output));
  OSPLAY_CHECK(suite, read);
  OSPLAY_CHECK(suite, (*read) == sizeof(output));
  for (u32 index = 0; index < 124; ++index) {
    OSPLAY_CHECK(suite, output[index] == 0x11);
  }
  for (u32 index = 124; index < 1148; ++index) {
    OSPLAY_CHECK(suite, output[index] == 0);
  }
  for (u32 index = 1148; index < sizeof(output); ++index) {
    OSPLAY_CHECK(suite, output[index] == 0x33);
  }

  const auto eof =
      mounted->read((*inode), 3 * 1024, output, 1);
  OSPLAY_CHECK(suite, eof);
  OSPLAY_CHECK(suite, (*eof) == 0);
}

}  // namespace

int main() {
  osplay::test::Suite suite{"drivers/fs/ext4/file"};
  check_sparse_partial_read(suite);
  return suite.finish();
}
