#include "test_support.hpp"

#include <support/test.hpp>

namespace {

using namespace osplay;
using namespace osplay::drivers::fs;
using namespace osplay::drivers::fs::ext4;

void build_tree(ext4::test::Image& image) {
  image.extent_inode(3, 0x81a4, 5, 11, 1);
  image.extent_inode(4, 0x41ed, 1024, 12, 1);
  image.extent_inode(5, 0x81a4, 4, 13, 1);
  image.directory_entry(10, 0, 2, 12, ".", 2);
  image.directory_entry(10, 12, 2, 12, "..", 2);
  image.directory_entry(10, 24, 3, 20, "alpha.txt", 1);
  image.directory_entry(10, 44, 4, 980, "sub", 2);
  image.directory_entry(12, 0, 4, 12, ".", 2);
  image.directory_entry(12, 12, 2, 12, "..", 2);
  image.directory_entry(12, 24, 5, 1000, "beta", 1);
}

void check_paths(osplay::test::Suite& suite) {
  ext4::test::Image image;
  build_tree(image);
  auto mounted = Volume<ext4::test::Device>::mount(image.device);
  OSPLAY_CHECK(suite, mounted);
  auto node = mounted->lookup_path("/alpha.txt");
  OSPLAY_CHECK(suite, node);
  OSPLAY_CHECK(suite, node->inode == 3);
  node = mounted->lookup_path("//sub/beta");
  OSPLAY_CHECK(suite, node);
  OSPLAY_CHECK(suite, node->inode == 5);
  node = mounted->lookup_path("sub/./beta");
  OSPLAY_CHECK(suite, node);
  OSPLAY_CHECK(suite, node->inode == 5);
  node = mounted->lookup_path("/sub/../alpha.txt");
  OSPLAY_CHECK(suite, node);
  OSPLAY_CHECK(suite, node->inode == 3);

  const auto wrong_case = mounted->lookup_path("/ALPHA.TXT");
  OSPLAY_CHECK(suite, !wrong_case);
  OSPLAY_CHECK(suite, wrong_case.error() == Error::not_found);
  const auto through_file =
      mounted->lookup_path("/alpha.txt/child");
  OSPLAY_CHECK(suite, !through_file);
  OSPLAY_CHECK(suite, through_file.error() == Error::not_directory);
}

void check_corrupt_record(osplay::test::Suite& suite) {
  ext4::test::Image image;
  image.directory_entry(10, 0, 2, 10, ".", 2);
  auto mounted = Volume<ext4::test::Device>::mount(image.device);
  const auto root = mounted->root();
  const Error error =
      mounted->for_each((*root), [](const Entry&) { return true; });
  OSPLAY_CHECK(suite, error == Error::corrupt);
}

}  // namespace

int main() {
  osplay::test::Suite suite{"drivers/fs/ext4/directory"};
  check_paths(suite);
  check_corrupt_record(suite);
  return suite.finish();
}
