#include <array>
#include <cstring>
#include <drivers/fs/root.hpp>
#include <drivers/storage/root_device.hpp>
#include <drivers/storage/sector_reader.hpp>
#include <osplay/abi/loop.hpp>
#include <string_view>

namespace osplay::drivers::fs::root {
namespace {
class RootFilesystem;
// A single Volume instantiation serves hardware and file-backed devices.
class ByteDevice {
public:
  RootFilesystem *owner{};
  u32 index{loop_capacity};
  u64 size() const;
  bool read(u64 offset, u8 *output, u32 count);
  bool write(u64 offset, const u8 *input, u32 count);
  bool flush();
};
using Volume = ext4::Volume<ByteDevice>;
bool within(std::string_view path, std::string_view target) {
  return path.starts_with(target) &&
         (path.size() == target.size() || path[target.size()] == '/');
}
class RootFilesystem {
public:
  struct Loop {
    ByteDevice device;
    Volume volume;
    LoopStatus status;
    u8 *memory{};
    u64 memory_size{};
    char target[256]{};
    char type[5]{"ext4"};
    u32 opens{}, files{}, generation{};
    bool attached{}, mounted{}, backing_read_only{}, mount_read_only{};
  };
  storage::root_device::Device block_device;
  storage::SectorReader<storage::root_device::Device> reader{block_device};
  ByteDevice device;
  Volume volume;
  std::array<Loop, loop_capacity> loops{};
  bool ready{};
  constexpr RootFilesystem() = default;
  RootFilesystem(const RootFilesystem &) = delete;
  RootFilesystem &operator=(const RootFilesystem &) = delete;
  bool initialize() {
    ready = false;
    device.owner = this;
    reader.invalidate();
    for (u32 i = 0; i < loop_capacity; ++i) {
      const u32 generation = loops[i].generation + 1;
      loops[i] = {};
      loops[i].generation = generation;
      loops[i].device.owner = this;
      loops[i].device.index = i;
    }
    if (!block_device.initialize() || volume.initialize(device) != Error::none)
      return false;
    const auto root = volume.root();
    return ready = root && root->directory();
  }
  u32 route(const char *path) const {
    u32 result = loop_capacity;
    usize length = 0;
    for (u32 i = 0; i < loop_capacity; ++i)
      if (loops[i].mounted && within(path, loops[i].target) &&
          std::strlen(loops[i].target) > length) {
        result = i;
        length = std::strlen(loops[i].target);
      }
    return result;
  }
  const char *relative(u32 index, const char *path) const {
    if (index == loop_capacity)
      return path;
    const char *result = path + std::strlen(loops[index].target);
    return *result == '\0' ? "/" : result;
  }
  Volume *owner(const Node &node) {
    if (!ready)
      return nullptr;
    if (node.filesystem == 0)
      return &volume;
    if (node.filesystem > loop_capacity)
      return nullptr;
    auto &loop = loops[node.filesystem - 1];
    return loop.mounted && loop.generation == node.mount_generation
               ? &loop.volume
               : nullptr;
  }
  Result<Node> lookup(const char *path) {
    if (!ready || path == nullptr || path[0] != '/')
      return std::unexpected(Error::invalid_argument);
    const u32 index = route(path);
    auto inode = (index == loop_capacity ? volume : loops[index].volume)
                     .lookup_path(relative(index, path));
    if (!inode)
      return std::unexpected(inode.error());
    return Node{*inode, index == loop_capacity ? 0 : index + 1,
                index == loop_capacity ? 0 : loops[index].generation};
  }
  bool pinned(const Node &node) const {
    if (node.filesystem != 0)
      return false;
    for (const auto &loop : loops)
      if (loop.attached && !loop.memory && loop.status.backing.inode == node.inode)
        return true;
    return false;
  }
  bool readonly(const Node &node) const {
    return node.filesystem != 0 && node.filesystem <= loop_capacity &&
           loops[node.filesystem - 1].mount_read_only;
  }
  Error mutation(const Node &node) {
    return owner(node) == nullptr ? Error::invalid_argument
           : readonly(node)       ? Error::read_only
           : pinned(node)         ? Error::busy
                                  : Error::none;
  }
  Error path_mutation(const char *path) {
    if (!ready || path == nullptr || path[0] != '/')
      return Error::invalid_argument;
    const u32 index = route(path);
    return index != loop_capacity && loops[index].mount_read_only
               ? Error::read_only
               : Error::none;
  }
  u64 loop_size(u32 index) const {
    if (index >= loop_capacity || !loops[index].attached)
      return 0;
    const auto &status = loops[index].status;
    if (loops[index].memory)
      return loops[index].memory_size;
    const u64 available = status.backing.size - status.offset;
    const u64 size = status.size_limit == 0 || status.size_limit > available
                         ? available
                         : status.size_limit;
    return size - size % 512;
  }
  Result<u32> loop_read(u32 index, u64 offset, u8 *output, u32 count) {
    if (output == nullptr && count != 0)
      return std::unexpected(Error::invalid_argument);
    if (index >= loop_capacity || !loops[index].attached)
      return std::unexpected(Error::not_found);
    const u64 size = loop_size(index);
    if (offset > size || count > size - offset)
      return std::unexpected(Error::out_of_bounds);
    if (loops[index].memory) {
      if (count != 0) std::memcpy(output, loops[index].memory + offset, count);
      return count;
    }
    return volume.read(loops[index].status.backing,
                       loops[index].status.offset + offset, output, count);
  }
  Result<u32> loop_write(u32 index, u64 offset, const u8 *input, u32 count,
                         bool internal) {
    if (input == nullptr && count != 0)
      return std::unexpected(Error::invalid_argument);
    if (index >= loop_capacity || !loops[index].attached)
      return std::unexpected(Error::not_found);
    auto &loop = loops[index];
    if (loop.status.flags & abi::loop::read_only)
      return std::unexpected(Error::read_only);
    if (loop.mounted && !internal)
      return std::unexpected(Error::busy);
    const u64 size = loop_size(index);
    if (offset > size || count > size - offset)
      return std::unexpected(Error::out_of_bounds);
    if (loop.memory) {
      if (count != 0) std::memcpy(loop.memory + offset, input, count);
      return count;
    }
    return volume.write(loop.status.backing, loop.status.offset + offset, input,
                        count);
  }
  void autoclear(u32 index) {
    auto &loop = loops[index];
    if (loop.attached && !loop.mounted && loop.opens == 0 &&
        (loop.status.flags & abi::loop::autoclear)) {
      loop.attached = false;
      loop.status = {};
      loop.memory = nullptr;
      loop.memory_size = 0;
    }
  }
};
u64 ByteDevice::size() const {
  return index == loop_capacity ? owner->reader.size()
                                : owner->loop_size(index);
}
bool ByteDevice::read(u64 offset, u8 *output, u32 count) {
  if (index == loop_capacity)
    return owner->reader.read(offset, output, count);
  const auto result = owner->loop_read(index, offset, output, count);
  return result && *result == count;
}
bool ByteDevice::write(u64 offset, const u8 *input, u32 count) {
  if (index == loop_capacity)
    return owner->reader.write(offset, input, count);
  const auto result = owner->loop_write(index, offset, input, count, true);
  return result && *result == count;
}
bool ByteDevice::flush() {
  return index == loop_capacity ? owner->reader.flush()
                                : owner->volume.sync() == Error::none;
}
constinit RootFilesystem filesystem;
} // namespace
bool initialize() { return filesystem.initialize(); }
Result<Node> lookup(const char *path) { return filesystem.lookup(path); }
Error for_each(const Node &directory, void *context, DirectoryVisitor visitor) {
  auto *volume = filesystem.owner(directory);
  return volume == nullptr || visitor == nullptr
             ? Error::invalid_argument
             : volume->for_each(directory, [&](const ext4::Entry &entry) {
                 const Entry tagged{entry.name,
                                    Node{entry.node, directory.filesystem,
                                         directory.mount_generation},
                                    entry.directory_type};
                 return visitor(context, tagged);
               });
}
Result<u32> read(const Node &file, u64 offset, u8 *output, u32 count) {
  auto *volume = filesystem.owner(file);
  return volume ? volume->read(file, offset, output, count)
                : std::unexpected(Error::invalid_argument);
}
Result<u32> write(Node &file, u64 offset, const u8 *input, u32 count) {
  const Error error = filesystem.mutation(file);
  return error != Error::none
             ? Result<u32>{std::unexpected(error)}
             : filesystem.owner(file)->write(file, offset, input, count);
}
Error create(const char *path, const u8 *input, u32 count) {
  const Error error = filesystem.path_mutation(path);
  if (error != Error::none)
    return error;
  const u32 index = filesystem.route(path);
  return (index == loop_capacity ? filesystem.volume
                                 : filesystem.loops[index].volume)
      .create(filesystem.relative(index, path), input, count);
}
Error mkdir(const char *path, u16 mode) {
  const Error error = filesystem.path_mutation(path);
  if (error != Error::none)
    return error;
  const u32 index = filesystem.route(path);
  return (index == loop_capacity ? filesystem.volume
                                 : filesystem.loops[index].volume)
      .mkdir(filesystem.relative(index, path), mode);
}
Error truncate(Node &file, u64 size) {
  const Error error = filesystem.mutation(file);
  return error != Error::none ? error
                              : filesystem.owner(file)->truncate(file, size);
}
Error remove(const char *path) {
  const auto file = lookup(path);
  if (!file)
    return file.error();
  for (const auto &loop : filesystem.loops)
    if (loop.mounted && within(loop.target, path))
      return Error::busy;
  const Error error = filesystem.mutation(*file);
  if (error != Error::none)
    return error;
  const u32 index = filesystem.route(path);
  return filesystem.owner(*file)->remove(filesystem.relative(index, path));
}
Error move(const char *from, const char *to) {
  const auto file = lookup(from);
  if (!file)
    return file.error();
  for (const auto &loop : filesystem.loops)
    if (loop.mounted && (within(loop.target, from) || within(loop.target, to)))
      return Error::busy;
  const Error error = filesystem.mutation(*file);
  if (error != Error::none)
    return error;
  const Error destination_error = filesystem.path_mutation(to);
  if (destination_error != Error::none)
    return destination_error;
  const u32 index = filesystem.route(from);
  if (index != filesystem.route(to))
    return Error::cross_device;
  const auto destination = lookup(to);
  if (destination && filesystem.pinned(*destination))
    return Error::busy;
  return filesystem.owner(*file)->move(filesystem.relative(index, from),
                                       filesystem.relative(index, to));
}
Error sync() {
  if (!filesystem.ready)
    return Error::invalid_argument;
  for (auto &loop : filesystem.loops)
    if (loop.mounted && loop.volume.sync() != Error::none)
      return Error::io;
  return filesystem.volume.sync();
}
i32 free_loop() {
  for (u32 i = 0; i < loop_capacity; ++i)
    if (!filesystem.loops[i].attached)
      return static_cast<i32>(i);
  return -1;
}
Error loop_open(u32 index) {
  if (index >= loop_capacity)
    return Error::not_found;
  ++filesystem.loops[index].opens;
  return Error::none;
}
void loop_close(u32 index) {
  if (index < loop_capacity && filesystem.loops[index].opens != 0) {
    --filesystem.loops[index].opens;
    filesystem.autoclear(index);
  }
}
Error loop_attach(u32 index, const Node &backing, bool writable) {
  if (!filesystem.ready || index >= loop_capacity)
    return Error::invalid_argument;
  auto &loop = filesystem.loops[index];
  if (loop.attached || filesystem.pinned(backing))
    return Error::busy;
  // Reject backing files on loop mounts: no dependency cycles or unbounded
  // recursion.
  if (backing.filesystem != 0)
    return Error::unsupported;
  if (backing.type != Type::regular || backing.size < 512)
    return Error::invalid_argument;
  loop.status = {};
  loop.status.backing = backing;
  loop.backing_read_only = !writable;
  loop.status.flags = writable ? 0 : abi::loop::read_only;
  loop.attached = true;
  return Error::none;
}
Error loop_configure(u32 index, u64 offset, u64 limit, u32 flags,
                     const char *name) {
  if (index >= loop_capacity || !filesystem.loops[index].attached)
    return Error::not_found;
  auto &loop = filesystem.loops[index];
  if (loop.memory)
    return Error::unsupported;
  if (loop.mounted)
    return Error::busy;
  if ((flags & ~(abi::loop::read_only | abi::loop::autoclear)) != 0 ||
      offset > loop.status.backing.size || offset % 512 != 0 ||
      (limit != 0 && limit % 512 != 0))
    return Error::invalid_argument;
  if (loop.status.backing.size - offset < 512)
    return Error::invalid_argument;
  loop.status.offset = offset;
  loop.status.size_limit = limit;
  loop.status.flags =
      flags | (loop.backing_read_only ? abi::loop::read_only : 0);
  if (name != nullptr)
    std::memcpy(loop.status.name, name, sizeof(loop.status.name));
  loop.status.name[sizeof(loop.status.name) - 1] = '\0';
  return Error::none;
}
Result<LoopStatus> loop_status(u32 index) {
  if (index < loop_capacity && filesystem.loops[index].memory)
    return std::unexpected(Error::unsupported);
  return index < loop_capacity && filesystem.loops[index].attached
             ? Result<LoopStatus>{filesystem.loops[index].status}
             : Result<LoopStatus>{std::unexpected(Error::not_found)};
}
Error loop_detach(u32 index) {
  if (index >= loop_capacity || !filesystem.loops[index].attached)
    return Error::not_found;
  auto &loop = filesystem.loops[index];
  if (loop.mounted || loop.opens > 1)
    return Error::busy;
  const Error error = filesystem.volume.sync();
  if (error != Error::none)
    return error;
  loop.attached = false;
  loop.status = {};
  loop.memory = nullptr;
  loop.memory_size = 0;
  return Error::none;
}
u64 loop_size(u32 index) { return filesystem.loop_size(index); }
Result<u32> loop_read(u32 index, u64 offset, u8 *output, u32 count) {
  return filesystem.loop_read(index, offset, output, count);
}
Result<u32> loop_write(u32 index, u64 offset, const u8 *input, u32 count) {
  return filesystem.loop_write(index, offset, input, count, false);
}
Error mount_loop(u32 index, const char *target, bool ro, const char *type) {
  if (index >= loop_capacity || !filesystem.loops[index].attached)
    return Error::not_found;
  if (target == nullptr || target[0] != '/' || std::strlen(target) >= 256 ||
      std::strcmp(target, "/") == 0 || target[std::strlen(target) - 1] == '/')
    return Error::invalid_argument;
  if (type == nullptr ||
      (std::strcmp(type, "ext4") != 0 && std::strcmp(type, "ext2") != 0))
    return Error::unsupported;
  auto &loop = filesystem.loops[index];
  if (loop.mounted)
    return Error::busy;
  for (const auto &other : filesystem.loops)
    if (other.mounted && std::strcmp(other.target, target) == 0)
      return Error::busy;
  const auto directory = lookup(target);
  if (!directory)
    return directory.error();
  if (!directory->directory())
    return Error::not_directory;
  const Error error = loop.volume.initialize(loop.device);
  if (error != Error::none)
    return error;
  const auto root = loop.volume.root();
  if (!root || !root->directory())
    return root ? Error::invalid_format : root.error();
  if (!ro && !(loop.status.flags & abi::loop::read_only) &&
      !loop.volume.writable_format())
    return Error::read_only;
  ++loop.generation;
  if (loop.generation == 0)
    ++loop.generation;
  std::strcpy(loop.target, target);
  std::strcpy(loop.type, type);
  loop.mount_read_only = ro || (loop.status.flags & abi::loop::read_only);
  loop.mounted = true;
  return Error::none;
}
Error unmount(const char *target) {
  if (target == nullptr)
    return Error::invalid_argument;
  for (u32 i = 0; i < loop_capacity; ++i) {
    auto &loop = filesystem.loops[i];
    if (!loop.mounted || std::strcmp(loop.target, target) != 0)
      continue;
    if (loop.files != 0)
      return Error::busy;
    for (const auto &other : filesystem.loops)
      if (&other != &loop && other.mounted && within(other.target, loop.target))
        return Error::busy;
    const Error error = loop.volume.sync();
    if (error != Error::none)
      return error;
    loop.mounted = false;
    loop.target[0] = '\0';
    filesystem.autoclear(i);
    return Error::none;
  }
  return Error::invalid_argument;
}
Error mount_memory(u8 *bytes, u64 size, const char *target, bool ro) {
  if (!filesystem.ready || bytes == nullptr || size < 2048 || size % 512 != 0 ||
      size > static_cast<u64>(~usize{0}))
    return Error::invalid_argument;
  const i32 slot = free_loop();
  if (slot < 0)
    return Error::no_space;
  auto &loop = filesystem.loops[slot];
  loop.memory = bytes;
  loop.memory_size = size;
  loop.status = {};
  loop.status.flags = abi::loop::autoclear | (ro ? abi::loop::read_only : 0);
  loop.attached = true;
  const Error error = mount_loop(static_cast<u32>(slot), target, ro, "ext2");
  if (error != Error::none) {
    loop.attached = false;
    loop.memory = nullptr;
    loop.memory_size = 0;
    loop.status = {};
  }
  return error;
}
bool mount_contains(const char *path) {
  return path != nullptr && filesystem.route(path) != loop_capacity;
}
const char *mount_table() {
  static char text[4608]{};
  usize cursor = 0;
  const auto append = [&](const char *value, bool escape = false) {
    for (; *value != '\0' && cursor + 5 < sizeof(text); ++value) {
      const u8 byte = static_cast<u8>(*value);
      if (escape &&
          (byte == ' ' || byte == '\t' || byte == '\n' || byte == '\\')) {
        text[cursor++] = '\\';
        text[cursor++] = char('0' + ((byte >> 6) & 7));
        text[cursor++] = char('0' + ((byte >> 3) & 7));
        text[cursor++] = char('0' + (byte & 7));
      } else
        text[cursor++] = *value;
    }
  };
  append("/dev/root / ext4 rw 0 0\nproc /proc proc ro 0 0\nsysfs /sys sysfs ro "
         "0 0\n");
  for (u32 i = 0; i < loop_capacity; ++i) {
    const auto &loop = filesystem.loops[i];
    if (!loop.mounted)
      continue;
    append(loop.memory ? "/dev/ram" : "/dev/loop");
    text[cursor++] = char('0' + i);
    append(" ");
    append(loop.target, true);
    append(" ");
    append(loop.type);
    append(loop.mount_read_only ? " ro 0 0\n" : " rw 0 0\n");
  }
  text[cursor] = '\0';
  return text;
}
bool read_only(const Node &node) { return filesystem.readonly(node); }
Error retain(const Node &node) {
  if (filesystem.owner(node) == nullptr)
    return Error::invalid_argument;
  if (node.filesystem != 0)
    ++filesystem.loops[node.filesystem - 1].files;
  return Error::none;
}
void release(const Node &node) {
  if (node.filesystem != 0 && filesystem.owner(node) != nullptr) {
    auto &refs = filesystem.loops[node.filesystem - 1].files;
    if (refs != 0)
      --refs;
  }
}
} // namespace osplay::drivers::fs::root
