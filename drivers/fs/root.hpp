#pragma once

#include <drivers/fs/ext4/ext4.hpp>

namespace osplay::drivers::fs::root {

// An inode number alone is not an identity once multiple volumes are mounted.
// Generation prevents an old descriptor from addressing a reused mount slot.
struct Node : ext4::Node {
  u32 filesystem{};
  u32 mount_generation{};
  constexpr Node() = default;
  constexpr Node(const ext4::Node& inode, u32 fs = 0, u32 generation = 0)
      : ext4::Node(inode), filesystem(fs), mount_generation(generation) {}
};
struct Entry {
  Name name{};
  Node node{};
  u8 directory_type{};
};
using Type = ext4::Type;
using DirectoryVisitor = bool (*)(void* context, const Entry& entry);

[[nodiscard]] bool initialize();
[[nodiscard]] Result<Node> lookup(const char* path);
[[nodiscard]] Error for_each(const Node& directory, void* context,
                             DirectoryVisitor visitor);
[[nodiscard]] Result<u32> read(const Node& file, u64 offset, u8* output,
                               u32 count);
[[nodiscard]] Result<u32> write(Node& file, u64 offset, const u8* input,
                                u32 count);
[[nodiscard]] Error create(const char* path, const u8* input, u32 count);
[[nodiscard]] Error mkdir(const char* path, u16 mode);
[[nodiscard]] Error truncate(Node& file, u64 size);
[[nodiscard]] Error remove(const char* path);
[[nodiscard]] Error move(const char* source, const char* destination);
[[nodiscard]] Error sync();

inline constexpr u32 loop_capacity = 4;
struct LoopStatus {
  Node backing{};
  u64 offset{};
  u64 size_limit{};
  u32 flags{};
  char name[64]{};
};
[[nodiscard]] i32 free_loop();
[[nodiscard]] Error loop_open(u32 index);
void loop_close(u32 index);
[[nodiscard]] Error loop_attach(u32 index, const Node& backing, bool writable);
[[nodiscard]] Error loop_configure(u32 index, u64 offset, u64 size_limit,
                                   u32 flags, const char* name);
[[nodiscard]] Result<LoopStatus> loop_status(u32 index);
[[nodiscard]] Error loop_detach(u32 index);
[[nodiscard]] u64 loop_size(u32 index);
[[nodiscard]] Result<u32> loop_read(u32 index, u64 offset, u8* output, u32 count);
[[nodiscard]] Result<u32> loop_write(u32 index, u64 offset, const u8* input, u32 count);
[[nodiscard]] Error mount_loop(u32 index, const char* target, bool read_only,
                               const char* type = "ext4");
// Mount an existing memory image without allocating or copying its contents.
// The caller owns the bytes until unmount; the ordinary Volume/parser is used.
[[nodiscard]] Error mount_memory(u8* bytes, u64 size, const char* target,
                                 bool read_only = true);
[[nodiscard]] Error unmount(const char* target);
[[nodiscard]] bool mount_contains(const char* path);
[[nodiscard]] const char* mount_table();
[[nodiscard]] bool read_only(const Node& node);
[[nodiscard]] Error retain(const Node& node);
void release(const Node& node);

}  // namespace osplay::drivers::fs::root
