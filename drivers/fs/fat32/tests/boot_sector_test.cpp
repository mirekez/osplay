#include "test_support.hpp"

#include <support/test.hpp>

namespace {

using namespace osplay;
using namespace osplay::drivers::fs;
using namespace osplay::drivers::fs::fat32;

void check_valid(osplay::test::Suite& suite) {
  fat32::test::Image image;
  const auto mounted = Volume<fat32::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  if (!mounted) {
    return;
  }
  const auto& geometry = mounted->geometry();
  OSPLAY_CHECK(suite, geometry.bytes_per_sector == 512);
  OSPLAY_CHECK(suite, geometry.bytes_per_cluster == 512);
  OSPLAY_CHECK(suite, geometry.first_data_sector == 544);
  OSPLAY_CHECK(suite, geometry.cluster_count == 65525);
  OSPLAY_CHECK(suite, geometry.root_cluster == 2);
}

void check_sector_sizes(osplay::test::Suite& suite) {
  constexpr u32 sizes[] = {1024, 2048, 4096};
  for (u32 bytes_per_sector : sizes) {
    fat32::test::Image image;
    const u32 fat_sectors =
        ((fat32::test::Image::cluster_count + 2) * 4 +
         bytes_per_sector - 1) /
        bytes_per_sector;
    const u32 total_sectors =
        fat32::test::Image::reserved_sectors + fat_sectors +
        fat32::test::Image::cluster_count;
    image.device.write16(11, static_cast<u16>(bytes_per_sector));
    image.device.write32(32, total_sectors);
    image.device.write32(36, fat_sectors);
    image.device.resize(static_cast<u64>(total_sectors) *
                        bytes_per_sector);
    const auto mounted =
        Volume<fat32::test::Device>::mount(image.device);
    OSPLAY_CHECK(suite, mounted);
    if (mounted) {
      OSPLAY_CHECK(suite,
                  mounted->geometry().bytes_per_sector ==
                      bytes_per_sector);
    }
  }
}

void check_invalid(osplay::test::Suite& suite) {
  fat32::test::Image image;
  image.device.write8(510, 0);
  auto mounted = Volume<fat32::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, !mounted);
  OSPLAY_CHECK(suite, mounted.error() == Error::invalid_format);

  image.device.write8(510, 0x55);
  image.device.write16(11, 513);
  mounted = Volume<fat32::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, !mounted);
  OSPLAY_CHECK(suite, mounted.error() == Error::invalid_format);

  image.device.write16(11, 512);
  image.device.write8(13, 3);
  mounted = Volume<fat32::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, !mounted);

  image.device.write8(13, 1);
  image.device.write16(42, 1);
  mounted = Volume<fat32::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, !mounted);

  image.device.write16(42, 0);
  image.device.resize(4096);
  mounted = Volume<fat32::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, !mounted);
  OSPLAY_CHECK(suite, mounted.error() == Error::out_of_bounds);
}

void check_io_error(osplay::test::Suite& suite) {
  fat32::test::Image image;
  image.device.fail_reads(true);
  const auto mounted = Volume<fat32::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, !mounted);
  OSPLAY_CHECK(suite, mounted.error() == Error::io);
}

}  // namespace

int main() {
  osplay::test::Suite suite{"drivers/fs/fat32/boot_sector"};
  check_valid(suite);
  check_sector_sizes(suite);
  check_invalid(suite);
  check_io_error(suite);
  return suite.finish();
}
