#include "test_support.hpp"

#include <support/test.hpp>

namespace {

using namespace osplay;
using namespace osplay::drivers::fs;
using namespace osplay::drivers::fs::ext4;

struct CountingDevice {
  ext4::test::Device* device;
  u32 reads{};
  u32 unaligned_blocks{};
  u64 size() const { return device->size(); }
  bool read(u64 offset, u8* output, u32 count) {
    ++reads;
    if (count >= 512 && (reinterpret_cast<usize>(output) & 7u))
      ++unaligned_blocks;
    return device->read(offset, output, count);
  }
};

void check_external_contiguous_read(osplay::test::Suite& suite) {
  ext4::test::Image image;
  image.external_extent_inode(3, 0x81a4, 16 * 1024, 20, 21);
  // Replace the leaf's one-block extent with a sixteen-block contiguous run.
  image.device.write16(20 * 1024 + 12 + 4, 16);
  u8 data[1024];
  for (u32 block = 0; block < 16; ++block) {
    for (u32 byte = 0; byte < sizeof(data); ++byte)
      data[byte] = static_cast<u8>(block * 17 + byte);
    image.write_block(21 + block, data, sizeof(data));
  }
  CountingDevice device{&image.device};
  auto mounted = Volume<CountingDevice>::mount(device);
  OSPLAY_CHECK(suite, mounted);
  const auto inode = mounted->read_inode(3);
  OSPLAY_CHECK(suite, inode);
  alignas(8) u8 output[16 * 1024];
  device.reads = device.unaligned_blocks = 0;
  const auto read = mounted->read(*inode, 0, output, sizeof(output));
  OSPLAY_CHECK(suite, read && *read == sizeof(output));
  OSPLAY_CHECK(suite, device.reads == 2);  // One tree leaf and one data run.
  OSPLAY_CHECK(suite, device.unaligned_blocks == 0);
  for (u32 byte = 0; byte < sizeof(output); ++byte)
    OSPLAY_CHECK(suite, output[byte] == static_cast<u8>((byte / 1024) * 17 + byte % 1024));

  // A run may not bypass physical bounds even when the extent length claims more.
  image.device.write32(20 * 1024 + 12 + 8, 63);
  const auto invalid = mounted->read(*inode, 0, output, 2 * 1024);
  OSPLAY_CHECK(suite, !invalid && invalid.error() == Error::corrupt);
}

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
  check_external_contiguous_read(suite);
  return suite.finish();
}
