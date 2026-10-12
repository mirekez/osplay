#pragma once
#include <osplay/base.hpp>

namespace osplay::abi::loop {
// Linux UAPI include/uapi/linux/loop.h. All fields have fixed widths; no
// kernel pointers or C++ objects cross the userspace boundary.
inline constexpr u32 set_fd = 0x4c00;
inline constexpr u32 clear_fd = 0x4c01;
inline constexpr u32 set_status64 = 0x4c04;
inline constexpr u32 get_status64 = 0x4c05;
inline constexpr u32 configure = 0x4c0a;
inline constexpr u32 get_free = 0x4c82;
inline constexpr u32 read_only = 1;
inline constexpr u32 autoclear = 4;
struct Info64 {
  u64 device{}, inode{}, rdevice{}, offset{}, size_limit{};
  u32 number{}, encrypt_type{}, encrypt_key_size{}, flags{};
  u8 file_name[64]{}, crypt_name[64]{}, encrypt_key[32]{};
  u64 init[2]{};
};
struct Config {
  u32 fd{}, block_size{};
  Info64 info{};
  u64 reserved[8]{};
};
static_assert(sizeof(Info64) == 232);
static_assert(sizeof(Config) == 304);
} // namespace osplay::abi::loop
