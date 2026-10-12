#include <cstdlib>
#include <fstream>
#include <iterator>
#include <linux/loop.h>
#include <support/test.hpp>
#include <sys/mman.h>
#include <vector>

// Exercise production ext4, mount routing, descriptor ownership and ABI
// adapters with real mke2fs images; no substitute filesystem or mount model.
#include <drivers/fs/root.cpp>
static_assert(sizeof(osplay::abi::loop::Info64) == sizeof(loop_info64));
static_assert(sizeof(osplay::abi::loop::Config) == sizeof(loop_config));
static_assert(offsetof(osplay::abi::loop::Info64, file_name) ==
              offsetof(loop_info64, lo_file_name));
static_assert(osplay::abi::loop::configure == LOOP_CONFIGURE);
static_assert(osplay::abi::loop::get_free == LOOP_CTL_GET_FREE);
#include <kernel/syscall.cpp>
extern "C" {
unsigned char __fork_snapshot_arena_begin[1];
unsigned char __fork_snapshot_arena_end[1];
}
namespace {
std::vector<osplay::u8> disk_bytes;
bool fail_read{}, fail_write{}, fail_flush{};
} // namespace
namespace osplay {
Process process{};
void write_text(const char *) {}
void write_u32(u32) {}
namespace network {
SocketResult socket_retain(SocketHandle) { return SocketResult::success; }
SocketResult socket_close(SocketHandle) { return SocketResult::success; }
const char *tcp_table() { return ""; }
} // namespace network
namespace drivers::storage::root_device {
bool Device::initialize() { return true; }
u64 Device::sector_count() const { return disk_bytes.size() / 512; }
bool Device::read_sector(u64 sector, u8 *output) {
  return read_sectors(sector, output, 512);
}
bool Device::write_sector(u64 sector, const u8 *input) {
  return write_sectors(sector, input, 512);
}
bool Device::read_sectors(u64 sector, u8 *output, u32 size) {
  if (fail_read || sector > disk_bytes.size() / 512 ||
      size > disk_bytes.size() - sector * 512)
    return false;
  std::memcpy(output, disk_bytes.data() + sector * 512, size);
  return true;
}
bool Device::write_sectors(u64 sector, const u8 *input, u32 size) {
  if (fail_write || sector > disk_bytes.size() / 512 ||
      size > disk_bytes.size() - sector * 512)
    return false;
  std::memcpy(disk_bytes.data() + sector * 512, input, size);
  return true;
}
bool Device::flush() { return !fail_flush; }
} // namespace drivers::storage::root_device
} // namespace osplay
namespace {
using namespace osplay;
namespace root = drivers::fs::root;
using Error = drivers::fs::Error;
std::vector<u8> image(const char *name) {
  std::ifstream input(std::string(LOOP_FIXTURES) + "/" + name,
                      std::ios::binary);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}
u32 text(const char *value, u32 address = user_begin) {
  std::strcpy(reinterpret_cast<char *>(address), value);
  return address;
}
i32 open_file(const char *name, u32 flags = 2) {
  return osplay::openat(static_cast<u32>(-100), text(name), flags);
}
i32 mount_disk(const char *type = "ext4", u32 flags = 0) {
  return mount_filesystem(text("/dev/loop0"), text("/mnt", user_begin + 256),
                          text(type, user_begin + 512), flags, 0);
}
i32 unmount_disk() { return unmount_filesystem(text("/mnt"), 0); }
} // namespace
int main() {
  test::Suite suite{"kernel/loop_mount"};
  void *user =
      mmap(reinterpret_cast<void *>(user_begin), 65536, PROT_READ | PROT_WRITE,
           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  OSPLAY_CHECK(suite, user != MAP_FAILED);
  if (user == MAP_FAILED)
    return suite.finish();
  process = {};
  std::strcpy(process.current_directory, "/");
  const auto original = image("loop-root.ext4");
  OSPLAY_CHECK(suite, !original.empty());
  for (const char *format : {"loop.ext4", "loop.ext2"}) {
    reset_descriptors();
    disk_bytes = original;
    OSPLAY_CHECK(suite, root::initialize());
    const auto payload = image(format);
    OSPLAY_CHECK(suite, root::create("/disk", payload.data(), payload.size()) ==
                            Error::none);
    OSPLAY_CHECK(suite, root::create("/disk2", payload.data(),
                                     payload.size()) == Error::none);
    OSPLAY_CHECK(suite, root::mkdir("/mnt", 0755) == Error::none);
    OSPLAY_CHECK(suite, root::mkdir("/dev", 0755) == Error::none);
    OSPLAY_CHECK(suite, root::mkdir("/mnt-other", 0755) == Error::none);
    OSPLAY_CHECK(suite, root::create("/mnt/hidden", nullptr, 0) == Error::none);
    const i32 backing = open_file("/disk");
    const i32 loop = open_file("/dev/loop0");
    const i32 control = open_file("/dev/loop-control");
    OSPLAY_CHECK(suite, backing >= 3 && loop >= 3 && control >= 3);
    auto &slot = *descriptor_slot(loop);
    OSPLAY_CHECK(suite, fstat64(loop, user_begin + 4096) == 0);
    const auto *stat = reinterpret_cast<Stat64 *>(user_begin + 4096);
    OSPLAY_CHECK(suite, (stat->mode & 0170000) == 0060000 &&
                            stat->special_device == (7u << 8));
    const i32 dev = open_file("/dev", 0);
    const i32 directory_size = getdents64(dev, user_begin + 8192, 4096);
    OSPLAY_CHECK(suite, directory_size > 0);
    bool listed_loop = false;
    for (u32 pos = 0;
         directory_size > 0 && pos < static_cast<u32>(directory_size);) {
      const auto *entry = reinterpret_cast<const u8 *>(user_begin + 8192 + pos);
      if (std::strcmp(reinterpret_cast<const char *>(entry + 19), "loop0") == 0)
        listed_loop = entry[18] == 6;
      const u16 length = drivers::fs::little_u16(entry + 16);
      if (length == 0)
        break;
      pos += length;
    }
    OSPLAY_CHECK(suite, listed_loop && osplay::close(dev) == 0);
    OSPLAY_CHECK(suite, loop_ioctl(slot, abi::loop::get_status64,
                                   user_begin + 1024) == -6);
    OSPLAY_CHECK(suite, loop_ioctl(*descriptor_slot(control),
                                   abi::loop::get_free, 0) == 0);
    OSPLAY_CHECK(suite, loop_ioctl(slot, abi::loop::set_fd, 100) == -9);
    OSPLAY_CHECK(suite,
                 loop_ioctl(slot, abi::loop::configure, user_end) == -14);
    auto *config = reinterpret_cast<abi::loop::Config *>(user_begin + 1024);
    *config = {};
    config->fd = backing;
    config->block_size = 4096;
    OSPLAY_CHECK(suite, loop_ioctl(slot, abi::loop::configure,
                                   user_begin + 1024) == -22);
    OSPLAY_CHECK(suite, !root::loop_status(0));
    config->block_size = 512;
    config->info.flags = 16;
    OSPLAY_CHECK(suite, loop_ioctl(slot, abi::loop::configure,
                                   user_begin + 1024) == -22);
    OSPLAY_CHECK(suite, !root::loop_status(0));
    config->info.encrypt_type = 1;
    config->info.flags = 0;
    OSPLAY_CHECK(suite, loop_ioctl(slot, abi::loop::configure,
                                   user_begin + 1024) == -95);
    config->info.encrypt_type = 0;
    config->info.flags = abi::loop::autoclear;
    std::memcpy(config->info.file_name, "/disk", 6);
    OSPLAY_CHECK(
        suite, loop_ioctl(slot, abi::loop::configure, user_begin + 1024) == 0);
    OSPLAY_CHECK(suite, loop_ioctl(slot, abi::loop::get_status64,
                                   user_begin + 2048) == 0);
    const auto info = *reinterpret_cast<abi::loop::Info64 *>(user_begin + 2048);
    OSPLAY_CHECK(suite, info.inode != 0 && info.flags == abi::loop::autoclear);
    OSPLAY_CHECK(suite, root::loop_size(0) == payload.size());
    OSPLAY_CHECK(suite, loop_ioctl(slot, 0x80081272u, user_begin + 4096) == 0);
    OSPLAY_CHECK(suite,
                 *reinterpret_cast<u64 *>(user_begin + 4096) == payload.size());
    OSPLAY_CHECK(suite, root::loop_attach(1, *root::lookup("/disk"), true) ==
                            Error::busy);
    OSPLAY_CHECK(suite, root::remove("/disk") == Error::busy);
    auto file = *root::lookup("/disk");
    OSPLAY_CHECK(suite, root::truncate(file, 0) == Error::busy);
    OSPLAY_CHECK(suite, root::write(file, 0, payload.data(), 1).error() ==
                            Error::busy);
    OSPLAY_CHECK(suite, mount_disk("vfat") == -19);
    OSPLAY_CHECK(suite, mount_disk("ext4", 4096) == -22);
    OSPLAY_CHECK(suite, mount_disk() == 0);
    OSPLAY_CHECK(suite, mount_disk() == -16);
    OSPLAY_CHECK(suite, !root::lookup("/mnt/hidden"));
    OSPLAY_CHECK(suite, root::lookup("/mnt-other")->filesystem == 0);
    OSPLAY_CHECK(suite, root::lookup("/mnt")->filesystem == 1);
    root::Node visited;
    const auto remember = [](void *context, const root::Entry &entry) {
      *static_cast<root::Node *>(context) = entry.node;
      return false;
    };
    OSPLAY_CHECK(suite, root::for_each(*root::lookup("/mnt"), &visited,
                                       remember) == Error::none);
    OSPLAY_CHECK(suite,
                 visited.filesystem == 1 && visited.mount_generation != 0);
    OSPLAY_CHECK(suite, root::loop_attach(1, *root::lookup("/disk2"), true) ==
                            Error::none);
    OSPLAY_CHECK(suite, root::mkdir("/mnt/nest", 0755) == Error::none);
    OSPLAY_CHECK(suite, root::mount_loop(1, "/mnt/nest", false) == Error::none);
    OSPLAY_CHECK(suite, root::lookup("/mnt/nest")->filesystem == 2);
    OSPLAY_CHECK(suite, unmount_disk() == -16);
    OSPLAY_CHECK(suite, root::unmount("/mnt/nest") == Error::none);
    OSPLAY_CHECK(suite, root::loop_detach(1) == Error::none);
    OSPLAY_CHECK(suite, std::strstr(root::mount_table(),
                                    "/dev/loop0 /mnt ext4 rw") != nullptr);
    OSPLAY_CHECK(suite, loop_ioctl(slot, abi::loop::clear_fd, 0) == -16);
    OSPLAY_CHECK(suite, root::remove("/mnt") == Error::busy);
    const u8 contents[] = "loop write persistence";
    OSPLAY_CHECK(suite, root::create("/mnt/hello", contents,
                                     sizeof(contents)) == Error::none);
    std::vector<u8> nested_image(512);
    OSPLAY_CHECK(suite, root::create("/mnt/nested.img", nested_image.data(),
                                     nested_image.size()) == Error::none);
    OSPLAY_CHECK(suite, root::loop_attach(1, *root::lookup("/mnt/nested.img"),
                                          true) == Error::unsupported);
    OSPLAY_CHECK(suite, root::remove("/mnt/nested.img") == Error::none);
    OSPLAY_CHECK(suite,
                 root::move("/mnt/hello", "/outside") == Error::cross_device);
    OSPLAY_CHECK(suite, root::mkdir("/mnt/sub", 0755) == Error::none);
    std::vector<u8> big(32768, 0x7b);
    OSPLAY_CHECK(suite, root::create("/mnt/big", big.data(), big.size()) ==
                            Error::none);
    auto big_file = *root::lookup("/mnt/big");
    std::vector<u8> big_read(big.size());
    const auto received =
        root::read(big_file, 0, big_read.data(), big_read.size());
    OSPLAY_CHECK(suite, received && *received == big.size() && big_read == big);
    OSPLAY_CHECK(suite, root::truncate(big_file, 0) == Error::none);
    OSPLAY_CHECK(suite, root::remove("/mnt/big") == Error::none);
    const i32 opened = open_file("/mnt/hello", 0);
    OSPLAY_CHECK(suite, opened >= 3 && unmount_disk() == -16);
    const i32 duplicated = osplay::fcntl(opened, 0, 3);
    OSPLAY_CHECK(suite, duplicated >= 3);
    OSPLAY_CHECK(suite, osplay::close(opened) == 0 && unmount_disk() == -16);
    OSPLAY_CHECK(suite, osplay::close(duplicated) == 0);
    std::strcpy(process.current_directory, "/mnt/sub");
    OSPLAY_CHECK(suite, unmount_disk() == -16);
    std::strcpy(process.current_directory, "/");
    fail_flush = true;
    OSPLAY_CHECK(suite, unmount_disk() == -5 && root::mount_contains("/mnt"));
    fail_flush = false;
    const auto stale = *root::lookup("/mnt/hello");
    OSPLAY_CHECK(suite, unmount_disk() == 0);
    OSPLAY_CHECK(suite, root::lookup("/mnt/hidden"));
    OSPLAY_CHECK(suite, mount_disk("ext4", 1) == 0);
    OSPLAY_CHECK(suite, !root::read(stale, 0, big_read.data(), 1));
    const auto hello = *root::lookup("/mnt/hello");
    u8 buffer[64]{};
    OSPLAY_CHECK(suite, root::read(hello, 0, buffer, sizeof(contents)) ==
                            sizeof(contents));
    OSPLAY_CHECK(suite, std::memcmp(buffer, contents, sizeof(contents)) == 0);
    OSPLAY_CHECK(suite, open_file("/mnt/hello", 2) == -30);
    OSPLAY_CHECK(suite,
                 root::create("/mnt/ro", nullptr, 0) == Error::read_only);
    OSPLAY_CHECK(suite, unmount_disk() == 0);
    OSPLAY_CHECK(suite, osplay::close(loop) == 0 && !root::loop_status(0));
    OSPLAY_CHECK(suite, root::free_loop() == 0);
    OSPLAY_CHECK(suite,
                 osplay::close(backing) == 0 && osplay::close(control) == 0);
    file = *root::lookup("/disk");
    OSPLAY_CHECK(suite, root::loop_attach(0, file, false) == Error::none);
    OSPLAY_CHECK(suite, root::loop_configure(0, ~u64{0}, 0, 0, nullptr) ==
                            Error::invalid_argument);
    OSPLAY_CHECK(suite, root::loop_configure(0, 1, 0, 0, nullptr) ==
                            Error::invalid_argument);
    OSPLAY_CHECK(suite,
                 root::loop_configure(0, 512, 512, 0, nullptr) == Error::none);
    OSPLAY_CHECK(suite, root::loop_size(0) == 512);
    OSPLAY_CHECK(suite, !root::loop_read(0, 511, buffer, 2));
    OSPLAY_CHECK(suite, root::loop_write(0, 0, contents, 1).error() ==
                            Error::read_only);
    OSPLAY_CHECK(suite, root::mount_loop(0, "/mnt", false) != Error::none);
    OSPLAY_CHECK(suite, root::loop_detach(0) == Error::none);
    // Real RV32 llseek ABI: high/low offset, output pointer, SET/CUR/END.
    const i32 seek_fd = open_file("/disk");
    OSPLAY_CHECK(suite, seek_file(seek_fd, 0, 42, user_begin + 4096, 0) == 0);
    OSPLAY_CHECK(suite, *reinterpret_cast<u64 *>(user_begin + 4096) == 42);
    OSPLAY_CHECK(suite, seek_file(seek_fd, 0, 8, user_begin + 4096, 1) == 0);
    OSPLAY_CHECK(suite, *reinterpret_cast<u64 *>(user_begin + 4096) == 50);
    OSPLAY_CHECK(suite, seek_file(seek_fd, 0xffffffff, 0xffffffff,
                                  user_begin + 4096, 2) == 0);
    OSPLAY_CHECK(suite, *reinterpret_cast<u64 *>(user_begin + 4096) ==
                            payload.size() - 1);
    OSPLAY_CHECK(suite, seek_file(seek_fd, 1, 0, user_begin + 4096, 0) == -22);
    OSPLAY_CHECK(suite, seek_file(seek_fd, 0, 0, user_end, 0) == -14);
    OSPLAY_CHECK(suite, osplay::close(seek_fd) == 0);
    std::vector<u8> persisted(payload.size());
    const auto disk_file = *root::lookup("/disk");
    OSPLAY_CHECK(suite, root::read(disk_file, 0, persisted.data(),
                                   persisted.size()) == persisted.size());
    std::ofstream output(std::string(LOOP_FIXTURES) + "/result-" + format,
                         std::ios::binary);
    output.write(reinterpret_cast<const char *>(persisted.data()),
                 persisted.size());
    OSPLAY_CHECK(suite, output.good());
  }
  reset_descriptors();
  disk_bytes = original;
  OSPLAY_CHECK(suite, root::initialize());
  const auto payload = image("loop.ext4");
  for (u32 i = 0; i < root::loop_capacity; ++i) {
    char name[] = "/image0";
    name[6] = char('0' + i);
    OSPLAY_CHECK(suite, root::create(name, payload.data(), payload.size()) ==
                            Error::none);
    OSPLAY_CHECK(suite, root::loop_attach(i, *root::lookup(name), true) ==
                            Error::none);
  }
  OSPLAY_CHECK(suite, root::free_loop() == -1);
  OSPLAY_CHECK(suite,
               root::loop_attach(root::loop_capacity, *root::lookup("/image0"),
                                 true) == Error::invalid_argument);
  for (u32 i = 0; i < root::loop_capacity; ++i)
    OSPLAY_CHECK(suite, root::loop_detach(i) == Error::none);
  OSPLAY_CHECK(suite, root::free_loop() == 0);
  // A filesystem window must never overwrite either side of its backing file.
  std::vector<u8> window(payload.size() + 1024, 0xa5);
  std::copy(payload.begin(), payload.end(), window.begin() + 512);
  OSPLAY_CHECK(suite, root::create("/window.img", window.data(),
                                   window.size()) == Error::none);
  OSPLAY_CHECK(suite, root::mkdir("/window", 0755) == Error::none);
  const auto window_node = *root::lookup("/window.img");
  OSPLAY_CHECK(suite, root::loop_attach(0, window_node, true) == Error::none);
  OSPLAY_CHECK(suite, root::loop_configure(0, 512, payload.size(), 0,
                                           nullptr) == Error::none);
  OSPLAY_CHECK(suite, root::mount_loop(0, "/window", false) == Error::none);
  const u8 value[] = "within window";
  OSPLAY_CHECK(suite, root::create("/window/file", value, sizeof(value)) ==
                          Error::none);
  OSPLAY_CHECK(suite, root::unmount("/window") == Error::none);
  OSPLAY_CHECK(suite, root::loop_detach(0) == Error::none);
  u8 guards[512]{};
  OSPLAY_CHECK(suite, root::read(window_node, 0, guards, sizeof(guards)) ==
                          sizeof(guards));
  OSPLAY_CHECK(suite, std::all_of(std::begin(guards), std::end(guards),
                                  [](u8 byte) { return byte == 0xa5; }));
  OSPLAY_CHECK(suite, root::read(window_node, 512 + payload.size(), guards,
                                 sizeof(guards)) == sizeof(guards));
  OSPLAY_CHECK(suite, std::all_of(std::begin(guards), std::end(guards),
                                  [](u8 byte) { return byte == 0xa5; }));
  const auto backing_node = *root::lookup("/image0");
  OSPLAY_CHECK(suite, root::loop_attach(0, backing_node, true) == Error::none);
  alignas(8) u8 buffer[512]{};
  fail_read = true;
  OSPLAY_CHECK(suite, root::loop_read(0, 0, buffer, sizeof(buffer)).error() ==
                          Error::io);
  fail_read = false;
  fail_write = true;
  OSPLAY_CHECK(suite, root::loop_write(0, 0, buffer, sizeof(buffer)).error() ==
                          Error::io);
  fail_write = false;
  OSPLAY_CHECK(suite, root::loop_detach(0) == Error::none);
  reset_descriptors();
  munmap(user, 65536);
  return suite.finish();
}
