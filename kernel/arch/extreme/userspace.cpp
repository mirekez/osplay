#include <application.h>
#include <context/context.h>
#include <drivers/fs/root.hpp>
#include <drivers/uart/uart.hpp>
#include <kernel/arch/extreme/platform.hpp>
#include <kernel/arch/extreme/userspace.hpp>
#include <mikos/arch.hpp>
#include <mikos/kernel.hpp>

extern "C" void *memcpy(void *, const void *, mikos::usize);
extern "C" void *memset(void *, int, mikos::usize);
extern "C" unsigned __extreme_user_layout(unsigned);
extern "C" unsigned __extreme_activation_size(void (*)());
extern "C" unsigned __extreme_activation_end();

namespace mikos {
Process process{};
Scheduler scheduler{};
u32 user_stack_top{};
u32 user_heap_end{};
namespace extreme_platform {
alignas(EC_BUS_BYTES) u8 root_disk[root_disk_bytes];
alignas(4096) __attribute__((used, section(".extreme.heap"))) u8 heap[4 * 1024 * 1024];
namespace {
constexpr u32 activation_capacity = 4 * 1024 * 1024;
alignas(EC_BUS_BYTES)
    __attribute__((used, section(".extreme.activation"))) u8 user_activation[activation_capacity];
alignas(EC_BUS_BYTES) u8 kernel_activation[2 * 1024 * 1024];
alignas(EC_BUS_BYTES) u8 snapshots[8 * 1024 * 1024];
alignas(EC_BUS_BYTES) u8 initial_data[512 * 1024];

ExtremeContext boot_context;
TrapFrame active_frame{};
ExtremeApplication launch{};
u32 exit_status;
u32 data_begin, data_end;

bool text_equal(const char *a, const char *b) {
  while (*a && *a == *b) {
    ++a;
    ++b;
  }
  return *a == *b;
}
bool put_argument(const char *text, u32 &offset, unsigned &address) {
  address = offset;
  do {
    if (offset == sizeof(launch.text)) {
      return false;
    }
    launch.text[offset++] = *text;
  } while (*text++);
  return true;
}
[[noreturn]] void dispatch() {
  network::poll();
  active_frame.extreme_retry = true;
  active_frame.x[10] = static_cast<u32>(dispatch_syscall(active_frame));
  if (!process.image_replaced) {
    active_frame.extreme_retry = false;
  }
  process.image_replaced = false;
  if (active_frame.extreme_start) {
    active_frame.extreme_start = false;
    __extreme_context_enter(extreme_application_entry, user_activation);
  }
  deliver_pending_signal(active_frame);
  __extreme_context_restore(&active_frame.context, 1);
}
} // namespace
} // namespace extreme_platform

void uart_put(char value) { drivers::uart::put(static_cast<u8>(value)); }
void uart_write(const char *data, u32 size) {
  for (u32 i = 0; i < size; ++i) {
    uart_put(data[i]);
  }
}
void write_text(const char *text) { extreme_platform::write_text(text); }
void write_u32(u32 value) {
  char digits[10];
  unsigned count = 0;
  do {
    digits[count++] = '0' + value % 10;
    value /= 10;
  } while (value);
  while (count) {
    uart_put(digits[--count]);
  }
}
[[noreturn]] void shutdown(u32 code) {
  extreme_platform::exit_status = code;
  extreme_platform::write(::extreme::devices::exit_code, code);
  __extreme_context_restore(&extreme_platform::boot_context, 1);
}
void start_stress_ng(TrapFrame &) { shutdown(10); }
void restore_busybox_image() {}
bool restore_executable_image(const char *path) {
  return path != nullptr; // Native entries share immutable Extreme instruction memory.
}
bool replace_with_executable(TrapFrame &frame, const char *path, const char *const *arguments,
                             u32 argument_count, const char *const *environment,
                             u32 environment_count) {
  using namespace extreme_platform;
  if (!path || argument_count > EXTREME_ARGUMENTS || environment_count > EXTREME_ARGUMENTS) {
    return false;
  }
  auto file = drivers::fs::root::lookup(path);
  if (!file || file->type != drivers::fs::root::Type::regular) {
    return false;
  }
  char descriptor[16]{};
  auto read =
      drivers::fs::root::read(*file, 0, reinterpret_cast<u8 *>(descriptor), sizeof(descriptor) - 1);
  if (!read) {
    return false;
  }
  bool dropbear = text_equal(descriptor, "ECAPP\001dropbear\n");
  if (!dropbear && !text_equal(descriptor, "ECAPP\001busybox\n")) {
    return false;
  }
#ifndef EXTREME_DROPBEAR
  if (dropbear) {
    return false;
  }
#endif
  u32 cursor = 0;
  launch = {};
  launch.image = dropbear ? 1 : 0;
  launch.argc = argument_count;
  launch.envc = environment_count;
  for (u32 i = 0; i < argument_count; ++i) {
    if (!put_argument(arguments[i], cursor, launch.argv[i])) {
      return false;
    }
  }
  for (u32 i = 0; i < environment_count; ++i) {
    if (!put_argument(environment[i], cursor, launch.envp[i])) {
      return false;
    }
  }
  char executable[256];
  u32 length = 0;
  do {
    if (length == sizeof(executable)) {
      return false;
    }
    executable[length] = path[length];
  } while (path[length++]);
  memcpy(reinterpret_cast<void *>(data_begin), initial_data, data_end - data_begin);
  process.brk = data_end;
  process.mmap_begin = data_end + 1024 * 1024;
  process.mmap_cursor = process.mmap_begin;
  process.mutable_begin = data_begin;
  process.image = launch.image;
  process.image_replaced = true;
  memcpy(process.executable_path, executable, length);
  frame = {};
  frame.x[2] = reinterpret_cast<u32>(user_activation);
  frame.extreme_start = true;
  return true;
}
} // namespace mikos

extern "C" const ExtremeApplication *extreme_application() {
  return &mikos::extreme_platform::launch;
}
extern "C" unsigned char *extreme_snapshot_begin() { return mikos::extreme_platform::snapshots; }
extern "C" unsigned char *extreme_snapshot_end() {
  return mikos::extreme_platform::snapshots + sizeof(mikos::extreme_platform::snapshots);
}
extern "C" long __extreme_syscall(long number, long a, long b, long c, long d, long e, long f) {
  using namespace mikos::extreme_platform;
  do {
    mikos::user_stack_top = __extreme_activation_end();
    active_frame.x[17] = number;
    active_frame.x[10] = a;
    active_frame.x[11] = b;
    active_frame.x[12] = c;
    active_frame.x[13] = d;
    active_frame.x[14] = e;
    active_frame.x[15] = f;
    active_frame.x[2] = reinterpret_cast<unsigned>(user_activation);
    if (__extreme_context_save(&active_frame.context) == 0) {
      __extreme_context_enter(dispatch, kernel_activation);
    }
  } while (active_frame.extreme_retry);
  if (active_frame.extreme_signal_handler) {
    auto handler = reinterpret_cast<void (*)(int)>(active_frame.extreme_signal_handler);
    unsigned signal = active_frame.extreme_signal_number;
    active_frame.extreme_signal_handler = 0;
    handler(signal);
    __extreme_syscall(139, 0, 0, 0, 0, 0, 0);
    __builtin_trap();
  }
  return static_cast<long>(active_frame.x[10]);
}
namespace mikos::arch {
void start_scheduler_timer() {}
void rearm_scheduler_timer() {}
void set_network_timer_waiting(bool) {}
u64 time_ticks() { return extreme_platform::read(::extreme::devices::timer); }
} // namespace mikos::arch
extern "C" unsigned kernel() {
  using namespace mikos;
  using namespace mikos::extreme_platform;
  if (__extreme_context_save(&boot_context)) {
    return exit_status;
  }
  if (!drivers::uart::initialize()) {
    return 1;
  }
  mikos::write_text("MIKOS:BOOT\nMIKOS:EXTREME_USERSPACE\nMIKOS:UART_READY\n");
  mikos::write_text("MIKOS:MEMORY_INIT checking userspace layout\n");
  // Taking the address keeps the reserved heap in the linked image.
  heap[0] = 0;
  data_begin = __extreme_user_layout(0);
  data_end = __extreme_user_layout(1);
  user_heap_end = __extreme_user_layout(2);
  unsigned user_required = __extreme_activation_size(extreme_application_entry);
  unsigned kernel_required = __extreme_activation_size(dispatch);
  mikos::write_text("MIKOS:ACTIVATION user=");
  write_u32(user_required);
  mikos::write_text(" kernel=");
  write_u32(kernel_required);
  mikos::write_text("\n");
  if (data_end - data_begin > sizeof(initial_data) || user_required > activation_capacity ||
      kernel_required > sizeof(kernel_activation)) {
    mikos::write_text("MIKOS:MEMORY_FAIL activation or initial data exceeds capacity\n");
    shutdown(11);
  }
  mikos::write_text("MIKOS:MEMORY_INIT saving initial userspace data\n");
  memcpy(initial_data, reinterpret_cast<const void *>(data_begin), data_end - data_begin);
  user_stack_top = reinterpret_cast<u32>(user_activation) + sizeof(user_activation);
  mikos::write_text("MIKOS:MEMORY_READY\nMIKOS:ROOTFS_INIT mounting root filesystem\n");
  if (!drivers::fs::root::initialize()) {
    mikos::write_text("MIKOS:ROOTFS_FAIL\n");
    shutdown(12);
  }
  mikos::write_text("MIKOS:ROOTFS_READY\nMIKOS:NETWORK_INIT\n");
  if (!network::initialize()) {
    mikos::write_text("MIKOS:NETWORK_FAIL\n");
    shutdown(13);
  }
  mikos::write_text("MIKOS:NETWORK_READY\nMIKOS:INIT loading /bin/sh /etc/init.d/rcS\n");
  process.pid = process.process_group = process.session = 1;
  process.current_directory[0] = '/';
  const char *args[] = {"/bin/sh", "/etc/init.d/rcS"};
  const char *environment[] = {"PATH=/bin:/usr/bin:/sbin:/usr/sbin", "HOME=/root", "TERM=vt100",
                               "PS1=/ # "};
  if (!replace_with_executable(active_frame, "/bin/sh", args, 2, environment, 4)) {
    mikos::write_text("MIKOS:INIT_FAIL cannot load /bin/sh\n");
    shutdown(14);
  }
  active_frame.extreme_start = false;
  mikos::write_text("MIKOS:INIT_READY entering userspace\n");
  __extreme_context_enter(extreme_application_entry, user_activation);
}
