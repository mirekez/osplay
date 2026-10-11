#include "test_support.hpp"

#include <support/test.hpp>

namespace {

using namespace osplay;
using namespace osplay::drivers::fs;
using namespace osplay::drivers::fs::ext4;

void check_valid(osplay::test::Suite& suite) {
  ext4::test::Image image;
  const auto mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  if (!mounted) {
    return;
  }
  const auto& geometry = mounted->geometry();
  OSPLAY_CHECK(suite, geometry.block_size == 1024);
  OSPLAY_CHECK(suite, geometry.block_count == 64);
  OSPLAY_CHECK(suite, geometry.group_count == 1);
  OSPLAY_CHECK(suite, geometry.inode_size == 128);
  OSPLAY_CHECK(suite, geometry.descriptor_size == 32);
  OSPLAY_CHECK(suite, geometry.integrity_verified);
}

void check_block_and_descriptor_sizes(osplay::test::Suite& suite) {
  for (u32 log_size = 1; log_size <= 2; ++log_size) {
    ext4::test::Image image;
    const u32 block_size = 1024u << log_size;
    image.device.write32(1024 + 0x14, 0);
    image.device.write32(1024 + 0x18, log_size);
    image.device.resize(static_cast<u64>(
                            ext4::test::Image::block_count) *
                        block_size);
    const auto mounted =
        Volume<ext4::test::Device>::mount(image.device);
    OSPLAY_CHECK(suite, mounted);
    if (mounted) {
      OSPLAY_CHECK(suite,
                  mounted->geometry().block_size == block_size);
    }
  }

  ext4::test::Image image;
  image.device.write32(1024 + 0x60, 0x42 | 0x80);
  image.device.write16(1024 + 0xfe, 64);
  auto mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  if (mounted) {
    OSPLAY_CHECK(suite, mounted->geometry().descriptor_size == 64);
  }

  image.device.write32(1024 + 0x64, 0x400);
  mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  if (mounted) {
    OSPLAY_CHECK(suite,
                mounted->geometry().metadata_checksums);
    OSPLAY_CHECK(suite,
                !mounted->geometry().integrity_verified);
  }
}

void check_feature_rejection(osplay::test::Suite& suite) {
  ext4::test::Image image;
  image.device.write32(1024 + 0x60, 0x42 | 0x10);
  auto mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, !mounted);
  OSPLAY_CHECK(suite, mounted.error() == Error::unsupported);

  image.device.write32(1024 + 0x60, 0x42);
  image.device.write32(1024 + 0x64, 0x200);
  mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, !mounted);
  OSPLAY_CHECK(suite, mounted.error() == Error::unsupported);
}

void check_bounds_and_io(osplay::test::Suite& suite) {
  ext4::test::Image image;
  image.device.resize(4096);
  auto mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, !mounted);
  OSPLAY_CHECK(suite, mounted.error() == Error::out_of_bounds);

  ext4::test::Image failed;
  failed.device.fail_reads(true);
  mounted = Volume<ext4::test::Device>::mount(failed.device);
  OSPLAY_CHECK(suite, !mounted);
  OSPLAY_CHECK(suite, mounted.error() == Error::io);
}

}  // namespace

int main() {
  osplay::test::Suite suite{"drivers/fs/ext4/superblock"};
  check_valid(suite);
  check_block_and_descriptor_sizes(suite);
  check_feature_rejection(suite);
  check_bounds_and_io(suite);
  return suite.finish();
}
