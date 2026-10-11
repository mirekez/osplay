#include "test_support.hpp"

#include <support/test.hpp>

namespace {

using namespace osplay;
using namespace osplay::drivers::fs;
using namespace osplay::drivers::fs::ext4;

void check_block_allocation_persists(osplay::test::Suite& suite) {
  ext4::test::Image image;
  auto mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  OSPLAY_CHECK(suite, mounted->writable_format());
  const auto allocated = mounted->allocate_block();
  OSPLAY_CHECK(suite, allocated);
  OSPLAY_CHECK(suite, (*allocated) == 11);
  OSPLAY_CHECK(suite, image.device.flush_count() == 2);

  auto remounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, remounted);
  const auto next = remounted->allocate_block();
  OSPLAY_CHECK(suite, next);
  OSPLAY_CHECK(suite, (*next) == 12);
  OSPLAY_CHECK(suite, remounted->geometry().free_blocks ==
                         ext4::test::Image::block_count - 13);
  OSPLAY_CHECK(suite,
              remounted->release_block((*allocated)) == Error::none);

  auto final_mount = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, final_mount);
  const auto reused = final_mount->allocate_block();
  OSPLAY_CHECK(suite, reused);
  OSPLAY_CHECK(suite, (*reused) == (*allocated));
  OSPLAY_CHECK(suite,
              final_mount->release_block(3) ==
                  Error::invalid_argument);
}

void check_write_failure_is_reported(osplay::test::Suite& suite) {
  ext4::test::Image image;
  auto mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  image.device.fail_writes(true);
  const auto allocated = mounted->allocate_block();
  OSPLAY_CHECK(suite, !allocated);
  OSPLAY_CHECK(suite, allocated.error() == Error::io);
}

void check_inode_allocate_release_and_reuse(osplay::test::Suite& suite) {
  ext4::test::Image image;
  auto mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  const auto first = mounted->allocate_inode();
  OSPLAY_CHECK(suite, first);
  OSPLAY_CHECK(suite, (*first) == 11);

  auto remounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, remounted);
  const auto second = remounted->allocate_inode();
  OSPLAY_CHECK(suite, second);
  OSPLAY_CHECK(suite, (*second) == 12);
  OSPLAY_CHECK(suite,
              remounted->release_inode((*first)) == Error::none);

  auto final_mount = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, final_mount);
  const auto reused = final_mount->allocate_inode();
  OSPLAY_CHECK(suite, reused);
  OSPLAY_CHECK(suite, (*reused) == 11);
  OSPLAY_CHECK(suite,
              final_mount->release_inode(2) ==
                  Error::invalid_argument);
}

void check_create_write_move_concatenate_remove(
    osplay::test::Suite& suite) {
  ext4::test::Image image;
  auto mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  static_assert(MutableFilesystem<Volume<ext4::test::Device>>);
  const u8 first[1500] = {1, 2, 3};
  const u8 second[900] = {4, 5, 6};
  OSPLAY_CHECK(suite,
              mounted->create("/alpha.bin", first, sizeof(first)) ==
                  Error::none);
  OSPLAY_CHECK(suite,
              mounted->create("/beta.bin", second, sizeof(second)) ==
                  Error::none);
  OSPLAY_CHECK(suite,
              mounted->move("/alpha.bin", "/moved.bin") ==
                  Error::none);
  OSPLAY_CHECK(suite,
              mounted->concatenate("/moved.bin", "/beta.bin") ==
                  Error::none);

  auto remounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, remounted);
  OSPLAY_CHECK(suite, remounted->consistent());
  const auto size = remounted->file_size("/moved.bin");
  OSPLAY_CHECK(suite, size);
  OSPLAY_CHECK(suite, (*size) == sizeof(first) + sizeof(second));
  u8 output[sizeof(first) + sizeof(second)]{};
  const auto read = remounted->read("/moved.bin", 0, output,
                                        sizeof(output));
  OSPLAY_CHECK(suite, read);
  OSPLAY_CHECK(suite, (*read) == sizeof(output));
  for (u32 index = 0; index < sizeof(first); ++index) {
    OSPLAY_CHECK(suite, output[index] == first[index]);
  }
  for (u32 index = 0; index < sizeof(second); ++index) {
    OSPLAY_CHECK(suite, output[sizeof(first) + index] == second[index]);
  }
  OSPLAY_CHECK(suite,
              remounted->remove("/beta.bin") == Error::none);
  auto final_mount = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, final_mount);
  OSPLAY_CHECK(suite,
              final_mount->lookup_path("/beta.bin").error() ==
                  Error::not_found);
}

void check_create_directories(osplay::test::Suite& suite) {
  ext4::test::Image image;
  auto mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  OSPLAY_CHECK(suite,
              mounted->mkdir("/tmp", 01777) == Error::none);
  OSPLAY_CHECK(suite,
              mounted->mkdir("/tmp", 0755) ==
                  Error::already_exists);
  OSPLAY_CHECK(suite,
              mounted->mkdir("/tmp/nested", 0750) == Error::none);

  auto remounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, remounted);
  const auto root = remounted->root();
  const auto temporary = remounted->lookup_path("/tmp");
  const auto nested = remounted->lookup_path("/tmp/nested");
  OSPLAY_CHECK(suite, root && root->links == 3);
  OSPLAY_CHECK(suite, temporary && temporary->directory());
  OSPLAY_CHECK(suite, temporary->mode == 041777);
  OSPLAY_CHECK(suite, temporary->links == 3);
  OSPLAY_CHECK(suite, temporary->size == ext4::test::Image::block_size);
  OSPLAY_CHECK(suite, nested && nested->directory());
  OSPLAY_CHECK(suite, nested->mode == 040750);
  const auto dot = remounted->lookup((*temporary), ".");
  const auto dot_dot = remounted->lookup((*temporary), "..");
  OSPLAY_CHECK(suite, dot && dot->inode == temporary->inode);
  OSPLAY_CHECK(suite, dot_dot && dot_dot->inode == root->inode);

  u8 descriptor[32]{};
  OSPLAY_CHECK(suite,
              image.device.read(2 * ext4::test::Image::block_size,
                                descriptor, sizeof(descriptor)));
  OSPLAY_CHECK(suite, little_u16(descriptor + 0x10) == 3);
  OSPLAY_CHECK(suite, remounted->consistent());
}

}  // namespace

int main() {
  osplay::test::Suite suite{"drivers/fs/ext4/mutation"};
  check_block_allocation_persists(suite);
  check_write_failure_is_reported(suite);
  check_inode_allocate_release_and_reuse(suite);
  check_create_write_move_concatenate_remove(suite);
  check_create_directories(suite);
  return suite.finish();
}
