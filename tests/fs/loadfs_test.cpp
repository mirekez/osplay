#include <drivers/fs/ext4/tests/test_support.hpp>
#include <drivers/fs/fat32/tests/test_support.hpp>

#include <support/test.hpp>

namespace {

using namespace osplay;
using namespace osplay::drivers::fs;

[[nodiscard]] u32 prbs_step(u32& state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

void fill_prbs(u8* output, u32 size, u32 seed) {
  u32 state = seed;
  for (u32 index = 0; index < size; ++index) {
    output[index] = static_cast<u8>(prbs_step(state) + index);
  }
}

template <MutableFilesystem Filesystem>
void verify_file(osplay::test::Suite& suite, Filesystem& filesystem,
                 const char* path, const u8* expected, u32 expected_size) {
  const auto size = filesystem.file_size(path);
  OSPLAY_CHECK(suite, size);
  if (!size) {
    return;
  }
  OSPLAY_CHECK(suite, (*size) == expected_size);
  u8 window[257]{};
  for (u32 offset = 0; offset < expected_size; offset += sizeof(window)) {
    const u32 remaining = expected_size - offset;
    const u32 count =
        remaining < sizeof(window) ? remaining : sizeof(window);
    const auto read = filesystem.read(path, offset, window, count);
    OSPLAY_CHECK(suite, read);
    if (!read) {
      return;
    }
    OSPLAY_CHECK(suite, (*read) == count);
    for (u32 index = 0; index < count; ++index) {
      OSPLAY_CHECK(suite, window[index] == expected[offset + index]);
    }
  }
  OSPLAY_CHECK(suite, filesystem.consistent());
}

template <MutableFilesystem Filesystem>
void run_load(osplay::test::Suite& suite, Filesystem& filesystem) {
  constexpr u32 first_size = 8191;
  constexpr u32 second_size = 12289;
  static u8 first[first_size];
  static u8 second[second_size];
  static u8 concatenated[first_size + second_size];
  fill_prbs(first, sizeof(first), 0x13579bdf);
  fill_prbs(second, sizeof(second), 0x2468ace1);
  for (u32 index = 0; index < first_size; ++index) {
    concatenated[index] = first[index];
  }
  for (u32 index = 0; index < second_size; ++index) {
    concatenated[first_size + index] = second[index];
  }

  OSPLAY_CHECK(suite,
              filesystem.create("/alpha.prbs", first, sizeof(first)) ==
                  Error::none);
  OSPLAY_CHECK(suite,
              filesystem.create("/beta.prbs", second, sizeof(second)) ==
                  Error::none);
  OSPLAY_CHECK(suite, filesystem.consistent());
  verify_file(suite, filesystem, "/alpha.prbs", first, sizeof(first));
  verify_file(suite, filesystem, "/beta.prbs", second, sizeof(second));

  OSPLAY_CHECK(suite,
              filesystem.move("/alpha.prbs", "/moved.prbs") ==
                  Error::none);
  OSPLAY_CHECK(suite,
              filesystem.file_size("/alpha.prbs").error() ==
                  Error::not_found);
  verify_file(suite, filesystem, "/moved.prbs", first, sizeof(first));

  OSPLAY_CHECK(suite,
              filesystem.concatenate("/moved.prbs", "/beta.prbs") ==
                  Error::none);
  verify_file(suite, filesystem, "/moved.prbs", concatenated,
              sizeof(concatenated));
  verify_file(suite, filesystem, "/beta.prbs", second, sizeof(second));

  OSPLAY_CHECK(suite,
              filesystem.remove("/beta.prbs") == Error::none);
  OSPLAY_CHECK(suite,
              filesystem.file_size("/beta.prbs").error() ==
                  Error::not_found);
  OSPLAY_CHECK(suite,
              filesystem.remove("/beta.prbs") == Error::not_found);
  verify_file(suite, filesystem, "/moved.prbs", concatenated,
              sizeof(concatenated));
  OSPLAY_CHECK(suite, filesystem.consistent());
}

}  // namespace

int main() {
  osplay::test::Suite suite{"fs/loadfs"};
  using Ext4Contract =
      ext4::Volume<ext4::test::Device>;
  using Fat32Contract =
      fat32::Volume<fat32::test::Device>;
  static_assert(MutableFilesystem<Ext4Contract>);
  static_assert(MutableFilesystem<Fat32Contract>);

  ext4::test::Image ext4_image;
  auto ext4_mount = Ext4Contract::mount(ext4_image.device);
  OSPLAY_CHECK(suite, ext4_mount);
  run_load(suite, (*ext4_mount));
  OSPLAY_CHECK(suite,
              ext4_mount->create("/Case", nullptr, 0) == Error::none);
  OSPLAY_CHECK(suite,
              ext4_mount->create("/case", nullptr, 0) == Error::none);
  auto ext4_remount = Ext4Contract::mount(ext4_image.device);
  OSPLAY_CHECK(suite, ext4_remount);
  OSPLAY_CHECK(suite,
              ext4_remount->file_size("/moved.prbs").value() == 20480);
  OSPLAY_CHECK(suite, ext4_remount->consistent());

  fat32::test::Image fat32_image;
  fat32_image.end_directory(2, 0);
  auto fat32_mount = Fat32Contract::mount(fat32_image.device);
  OSPLAY_CHECK(suite, fat32_mount);
  run_load(suite, (*fat32_mount));
  OSPLAY_CHECK(suite,
              fat32_mount->create("/Case", nullptr, 0) == Error::none);
  OSPLAY_CHECK(suite,
              fat32_mount->create("/case", nullptr, 0) ==
                  Error::already_exists);
  auto fat32_remount = Fat32Contract::mount(fat32_image.device);
  OSPLAY_CHECK(suite, fat32_remount);
  OSPLAY_CHECK(suite,
              fat32_remount->file_size("/moved.prbs").value() == 20480);
  OSPLAY_CHECK(suite, fat32_remount->consistent());
  return suite.finish();
}
