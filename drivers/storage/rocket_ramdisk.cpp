#include <drivers/storage/rocket_ramdisk.hpp>
#include <drivers/fs/root.hpp>
#include <osplay/kernel.hpp>
#include <cstring>

namespace osplay::drivers::storage {
namespace {
namespace root = fs::root;
bool read_exact(const root::Node& file, u64 offset, u8* output, u32 count) {
  const auto result = root::read(file, offset, output, count);
  return result && *result == count;
}

struct Check {
  u8 table[256*1024];
  u32 offsets[512];
  bool visited[512];
  u32 files, checked;
  u64 total;
  bool good;
};
[[gnu::section(".noinit")]] Check check;

bool check_entry(void* context, const root::Entry& entry) {
  auto& state = *static_cast<Check*>(context);
  // Probe records are name-sorted. Do not call lookup() inside for_each():
  // that would both rescan and overwrite the live directory iterator's buffer.
  u32 begin = 0, end = state.files;
  while (begin < end) {
    const u32 index = begin + (end - begin)/2;
    const u8* record = state.table + state.offsets[index];
    const u16 length = fs::little_u16(record);
    const u32 common = length < entry.name.size ? length : entry.name.size;
    int order = std::memcmp(entry.name.data, record + 12, common);
    if (order == 0) order = int(entry.name.size) - int(length);
    if (order < 0) { end = index; continue; }
    if (order > 0) { begin = index + 1; continue; }
    const u64 size = fs::little_u64(record + 2);
    const u16 count = fs::little_u16(record + 10);
    state.good = !state.visited[index] && entry.node.type == root::Type::regular &&
                 entry.node.size == size && !fs::add_overflows(state.total, size);
    const u8* sample = record + 12 + length;
    u8 actual[64];
    for (u32 part = 0; state.good && part < 4; ++part, sample += 72) {
      const u64 position = fs::little_u64(sample);
      state.good = position <= size && count <= size - position &&
                   read_exact(entry.node, position, actual, count) &&
                   std::memcmp(actual, sample + 8, count) == 0;
    }
    if (!state.good) {
      write_text("OSPLAY:WEIGHTS_MISMATCH ");
      write_text(entry.name.data);
      write_text("\n");
      return false;
    }
    state.visited[index] = true;
    state.total += size;
    if (++state.checked % 8 == 0) {
      write_text("OSPLAY:WEIGHTS_CHECKED ");
      write_u32(state.checked);
      write_text("\n");
    }
    break;
  }
  return true;
}

// Host checks every file byte. Rocket checks every file's size and four byte
// ranges, including indirect blocks/high physical addresses, in one directory
// pass rather than repeatedly looking up 147 files (quadratic guest work).
[[maybe_unused]] bool check_files() {
  const auto table = root::lookup("/weights/osplay-check.bin");
  const auto directory = root::lookup("/weights");
  if (!table || !directory || table->size < 20 || table->size > sizeof(check.table) ||
      !read_exact(*table, 0, check.table, static_cast<u32>(table->size)) ||
      std::memcmp(check.table, "OPDISK01", 8) != 0)
    return false;
  check.files = fs::little_u32(check.table + 8);
  const u64 expected_bytes = fs::little_u64(check.table + 12);
  if (check.files == 0 || check.files > 512)
    return false;
  u32 offset = 20;
  for (u32 index = 0; index < check.files; ++index) {
    if (table->size - offset < 12)
      return false;
    check.offsets[index] = offset;
    check.visited[index] = false;
    const u8* record = check.table + offset;
    const u16 length = fs::little_u16(record);
    const u64 size = fs::little_u64(record + 2);
    const u16 count = fs::little_u16(record + 10);
    const u32 bytes = 12 + length + 4*72;
    if (length == 0 || length > 246 || count != (size < 64 ? size : 64) ||
        bytes > table->size - offset)
      return false;
    for (u32 byte = 0; byte < length; ++byte)
      if (record[12 + byte] == '/' || record[12 + byte] == '\0')
        return false;
    if (index != 0) {
      const u8* prior = check.table + check.offsets[index - 1];
      const u16 prior_length = fs::little_u16(prior);
      const u32 common = length < prior_length ? length : prior_length;
      const int order = std::memcmp(prior + 12, record + 12, common);
      if (order > 0 || (order == 0 && prior_length >= length))
        return false;
    }
    offset += bytes;
  }
  if (offset != table->size)
    return false;
  check.good = true;
  check.checked = 0;
  check.total = 0;
  write_text("OSPLAY:WEIGHTS_PROBE_START\n");
  if (root::for_each(*directory, &check, check_entry) != fs::Error::none ||
      !check.good || check.checked != check.files || check.total != expected_bytes)
    return false;
  write_text("OSPLAY:WEIGHTS_FILES ");
  write_u32(check.files);
  write_text("\nOSPLAY:WEIGHTS_CHECK_OK\n");
  return true;
}
}

bool initialize_rocket_ramdisk() {
  auto* bytes = reinterpret_cast<u8*>(static_cast<usize>(rocket_ramdisk_base));
  if (fs::little_u16(bytes + 1024 + 0x38) != fs::ext4::detail::magic)
    return true;
  const fs::Error directory = root::mkdir("/weights", 0755);
  if (directory != fs::Error::none && directory != fs::Error::already_exists)
    return false;
  if (root::mount_memory(bytes, rocket_ramdisk_size, "/weights") != fs::Error::none)
    return false;
  write_text("OSPLAY:WEIGHTS_MOUNT_OK\n");
#if defined(OSPLAY_RV64) && !defined(OSPLAY_ROCKET_RAMDISK_TEST)
  // The dedicated disk test verifies every probe. Application boots keep the
  // same read-only mount without repeating that slow directory/sample scan.
  return true;
#else
  return check_files();
#endif
}
}
