#pragma once

#include <osplay/base.hpp>
#include <osplay/net/interface.hpp>
#include <osplay/net/socket.hpp>
#include <osplay/scheduler.hpp>
#ifdef OSPLAY_VENUS
#include <osplay/abi/venus.hpp>
#endif

namespace osplay {

#ifdef OSPLAY_EXTREME_USERSPACE
inline constexpr u32 ram_begin = 0;
inline constexpr u32 user_begin = 0;
inline constexpr u32 user_end = EC_BUS_BYTES * EC_BANK_WORDS * EC_BANKS;
extern u32 user_stack_top;
extern u32 user_heap_end;
#else
#ifdef OSPLAY_RV64
// The installed RV64 glibc uses medlow: keep flat userspace below 2 GiB.
inline constexpr u32 ram_begin = 0x40000000;
inline constexpr u32 user_begin = 0x41000000;
inline constexpr u32 user_end = 0x42000000;
inline constexpr u32 user_stack_top = 0x41fff000;
#else
inline constexpr u32 ram_begin = 0x80000000;
inline constexpr u32 user_begin = 0x81000000;
inline constexpr u32 user_end = 0x82000000;
inline constexpr u32 user_stack_top = 0x81fff000;
#endif
#endif
#ifdef OSPLAY_VENUS
// Keep executable/stack addresses low, and leave HTIF/rootfs/weights outside
// userspace. The additional arena is initially sufficient for accelerator
// BIST; the model's multi-GiB allocations need a separate RV64 extension.
struct UserMemory {
  [[nodiscard]] constexpr bool contains(uword address, uword size) const {
    return UserRange{user_begin, user_end}.contains(address, size) ||
           UserRange{abi::venus::heap_begin, abi::venus::heap_end}.contains(address, size);
  }
  [[nodiscard]] constexpr bool aligned(u32 address, u32 alignment) const {
    return UserRange{user_begin, user_end}.aligned(address, alignment);
  }
  [[nodiscard]] constexpr bool cache_access(u64 address, u64 bytes, u64 discard) const {
    const bool dma = address >= abi::venus::dma_begin && address <= abi::venus::dma_end &&
                     bytes <= abi::venus::dma_end - address;
    return discard <= 1 && (dma || contains(address, bytes));
  }
};
inline constexpr UserMemory user_memory{};
#else
inline constexpr UserRange user_memory{user_begin, user_end};
#endif

struct Process {
  u32 brk;
  u32 mmap_begin;
  u32 mmap_cursor;
  u32 mutable_begin;
  u32 exit_code;
  u32 image;
  u32 pid;
  u32 parent_pid;
  u32 process_group;
  u32 session;
  char current_directory[256];
  char executable_path[256];
  bool image_replaced;
};

extern Process process;
extern Scheduler scheduler;

void uart_put(char value);
void uart_write(const char* data, u32 size);
void write_text(const char* text);
void write_u32(u32 value);
[[noreturn]] void shutdown(u32 code);
iword dispatch_syscall(TrapFrame& frame);
void deliver_pending_signal(TrapFrame& frame);
void start_stress_ng(TrapFrame& frame);
void restore_busybox_image();
[[nodiscard]] bool restore_executable_image(const char* path);
[[nodiscard]] bool replace_with_executable(TrapFrame& frame,
                                           const char* path,
                                           const char* const* arguments,
                                           u32 argument_count,
                                           const char* const* environment = nullptr,
                                           u32 environment_count = 0);

}  // namespace osplay

namespace osplay::network {
[[nodiscard]] bool initialize();
[[nodiscard]] bool boot_probe();
void poll();
[[nodiscard]] InterfaceControlResult interface_ioctl(u32 request,
                                                     Ifreq32& value);
[[nodiscard]] OpenResult socket_open(abi::socket::Type type);
[[nodiscard]] SocketResult socket_retain(SocketHandle handle);
[[nodiscard]] SocketResult socket_close(SocketHandle handle);
[[nodiscard]] SocketResult socket_bind(SocketHandle handle, Endpoint local);
[[nodiscard]] SocketResult socket_listen(SocketHandle handle, u32 backlog);
[[nodiscard]] AcceptResult socket_accept(SocketHandle handle);
[[nodiscard]] ReadResult socket_read(SocketHandle handle, u8* output, u32 size);
[[nodiscard]] ReadResult socket_write(SocketHandle handle, const u8* input, u32 size);
[[nodiscard]] SocketResult socket_shutdown(SocketHandle handle, u32 how);
[[nodiscard]] const SocketSlot* socket_slot(SocketHandle handle);
[[nodiscard]] bool socket_readable(SocketHandle handle);
[[nodiscard]] bool socket_writable(SocketHandle handle);
[[nodiscard]] const char* tcp_table();
}

extern "C" {
void trap_handler(osplay::TrapFrame* frame);
[[noreturn]] void enter_user(osplay::u32 entry, osplay::u32 stack);
extern const osplay::u8 uncooperative_probe_begin[];
extern const osplay::u8 uncooperative_probe_end[];
}
