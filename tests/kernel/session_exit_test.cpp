#include <sys/mman.h>

#include <cstdlib>
#include <support/test.hpp>

// Test the production context switching and exit code, not a second model.
#include <kernel/syscall.cpp>

extern "C" {
unsigned char __fork_snapshot_arena_begin[1];
unsigned char __fork_snapshot_arena_end[1];
}

namespace osplay {
Process process{};
void write_text(const char*) {}
void write_u32(u32) {}
[[noreturn]] void shutdown(u32) { std::abort(); }
bool restore_executable_image(const char*) { return true; }
namespace arch {
u64 test_ticks{};
u64 time_ticks() { return test_ticks; }
}  // namespace arch
namespace network {
SocketResult socket_retain(SocketHandle) { return SocketResult::success; }
SocketResult socket_close(SocketHandle) { return SocketResult::success; }
}  // namespace network
namespace drivers::fs::root {
Error retain(const Node&) { return Error::none; }
void release(const Node&) {}
Error loop_open(u32) { return Error::none; }
void loop_close(u32) {}
}  // namespace drivers::fs::root
}  // namespace osplay

namespace {
using namespace osplay;
using namespace osplay::process_model;

struct Session {
  TrapFrame frame{};
  u32 listener{};
  u32 server{};
  u32 shell{};
  PtyHandle pty{};

  explicit Session(test::Suite& suite) {
    process = {};
    signals = {};
    active_signal_frame = {};
    process.pid = 1;
    process.process_group = process.session = 1;
    process.mutable_begin = user_begin;
    process.brk = user_begin + 128;
    process.mmap_begin = process.mmap_cursor = user_begin + 4096;
    frame.x[2] = user_stack_top - 64;
    listener = fork(suite);
    server = fork(suite);
    const auto master = ptys.open_master();
    OSPLAY_CHECK(suite, master.status == PtyStatus::success);
    pty = master.handle;
    OSPLAY_CHECK(suite, ptys.set_locked(pty, false) == PtyStatus::success);
    descriptors[4].node = Node::pty;
    descriptors[4].pty = pty;
    descriptors[4].pty_end = PtyEnd::master;
    shell = fork(suite);
    // The shell owns only the slave; Dropbear retains the master.
    release_descriptor(descriptors[4]);
    const auto slave = ptys.open_slave(pty.slot);
    OSPLAY_CHECK(suite, slave.status == PtyStatus::success);
    standard_redirects[0].node = Node::pty;
    standard_redirects[0].pty = pty;
    standard_redirects[0].pty_end = PtyEnd::slave;
  }

  u32 fork(test::Suite& suite) {
    frame.mepc = 0x81001000;
    frame.x[17] = static_cast<u32>(Syscall::clone);
    OSPLAY_CHECK(suite, clone(frame, 17, 0, 0) == 0);
    return process.pid;
  }

  void park(test::Suite& suite) {
    frame.x[17] = static_cast<u32>(Syscall::read);
    OSPLAY_CHECK(suite, park_interactive_child(frame, pty, PtyEnd::slave));
    OSPLAY_CHECK(suite, process.pid == server);
    OSPLAY_CHECK(suite, !parent.active);
    OSPLAY_CHECK(suite, nested_ancestor_depth == 2);
  }

  void finish(test::Suite& suite) {
    OSPLAY_CHECK(suite, process.pid == listener);
    OSPLAY_CHECK(suite, parent.active && parent.lineage.pid == 1);
    OSPLAY_CHECK(suite, nested_ancestor_depth == 0);
    OSPLAY_CHECK(suite, !interactive_child.used);
    OSPLAY_CHECK(suite, !interactive_parent.active);
    OSPLAY_CHECK(suite, !interactive_relay_service_pid);
    OSPLAY_CHECK(suite, ptys.used() == 0);
    // Reap the connection child, just as the persistent listener does.
    OSPLAY_CHECK(suite, waitid(1, server, user_begin, 4) == 0);
    OSPLAY_CHECK(suite, resume_after_child_exit(frame, 0));
    OSPLAY_CHECK(suite, process.pid == 1);
    OSPLAY_CHECK(suite, frame.x[10] == listener);
    OSPLAY_CHECK(suite, waitid(1, listener, user_begin, 4) == 0);
    OSPLAY_CHECK(suite, snapshot_arena.available() == snapshot_arena.capacity());
    for (const auto& child : waitable_children) {
      OSPLAY_CHECK(suite, !child.used);
    }
    // Only the top-level exit is left for the shutdown path.
    OSPLAY_CHECK(suite, !resume_after_child_exit(frame, 0));
  }
};
}  // namespace

int main() {
  test::Suite suite{"kernel/session_exit"};
  constexpr u32 arena_end = user_end + 2 * 1024 * 1024;
  void* memory = mmap(reinterpret_cast<void*>(user_begin), arena_end - user_begin,
                      PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
  if (memory == MAP_FAILED) {
    suite.check(false, "map RV32 user memory and snapshot arena");
    return suite.finish();
  }
  OSPLAY_CHECK(suite, snapshot_arena.initialize(user_end, arena_end) ==
                         SnapshotArenaStatus::success);
  snapshot_arena_initialized = true;

  // A normal child finishing immediately still returns its PID from clone.
  {
    Session session{suite};
    OSPLAY_CHECK(suite, resume_after_child_exit(session.frame, 0));
    OSPLAY_CHECK(suite, process.pid == session.server);
    OSPLAY_CHECK(suite, session.frame.mepc == 0x81001004);
    OSPLAY_CHECK(suite, session.frame.x[10] == session.shell);
    OSPLAY_CHECK(suite, resume_after_child_exit(session.frame, 0));
    session.finish(suite);
  }

  // Logout after PTY input must replay the server's select, preserving both
  // PC and arguments; returning a fake fork PID corrupts select's result.
  {
    Session session{suite};
    session.park(suite);
    const u8 input[] = {'e', 'x', 'i', 't', '\n'};
    OSPLAY_CHECK(suite, ptys.write(session.pty, PtyEnd::master, input, 5).size == 5);
    session.frame.mepc = 0x81002000;
    session.frame.x[17] = static_cast<u32>(Syscall::pselect6);
    session.frame.x[10] = 8;
    OSPLAY_CHECK(suite, resume_interactive_child_if_ready(session.frame));
    OSPLAY_CHECK(suite, process.pid == session.shell);
    OSPLAY_CHECK(suite, resume_after_child_exit(session.frame, 0));
    OSPLAY_CHECK(suite, process.pid == session.server);
    OSPLAY_CHECK(suite, session.frame.mepc == 0x81002000);
    OSPLAY_CHECK(suite, session.frame.x[10] == 8);
    OSPLAY_CHECK(suite, process.image_replaced);
    OSPLAY_CHECK(suite, resume_after_child_exit(session.frame, 0));
    session.finish(suite);
  }

  // A failed connection must unwind to the listener even though its parent
  // is stacked while the PTY shell is parked. Repeat to catch resource leaks.
  for (u32 attempt = 0; attempt < 6; ++attempt) {
    Session session{suite};
    session.park(suite);
    OSPLAY_CHECK(suite, resume_after_child_exit(session.frame, 1));
    session.finish(suite);
  }

  // The same disconnect while a foreground program is waiting below ash
  // must also release the separately parked shell and its saved descriptors.
  {
    Session session{suite};
    static_cast<void>(session.fork(suite));
    session.park(suite);
    OSPLAY_CHECK(suite, interactive_parent.active);
    OSPLAY_CHECK(suite, resume_after_child_exit(session.frame, 1));
    session.finish(suite);
  }
  // A timed PTY wait below ash must wake through Dropbear without input.
  // Cover both timeout ABIs and ensure select outputs are cleared only in
  // the restored child's memory, not the server's current address space.
  for (const auto call : {Syscall::ppoll, Syscall::ppoll64, Syscall::pselect6,
                          Syscall::pselect6_time64}) {
    for (u32 wake = 0; wake < 3; ++wake) {
      Session session{suite};
      const u32 program = session.fork(suite);
      session.frame.x[17] = static_cast<u32>(call);
      session.frame.x[10] = 1;
      const bool select = call == Syscall::pselect6 || call == Syscall::pselect6_time64;
      auto* sets = reinterpret_cast<u32*>(user_begin);
      if (select) {
        for (u32 i = 0; i < 3; ++i) {
          sets[i] = 1;
          session.frame.x[11 + i] = user_begin + i * sizeof(u32);
        }
      } else {
        auto* pollfd = reinterpret_cast<Pollfd32*>(user_begin);
        *pollfd = {0, 1, 0};
        session.frame.x[10] = user_begin;
        session.frame.x[11] = 1;
      }
      const u32 argument = session.frame.x[10];
      arch::test_ticks = 100;
      OSPLAY_CHECK(suite, park_interactive_child(session.frame, session.pty,
                                                PtyEnd::slave, 200));
      OSPLAY_CHECK(suite, process.pid == session.server);
      OSPLAY_CHECK(suite, interactive_parent.active);
      sets[0] = sets[1] = sets[2] = 0xdeadbeef;
      arch::test_ticks = 199;
      OSPLAY_CHECK(suite, !resume_interactive_child_if_ready(session.frame));
      if (wake == 1) {
        const u8 input[]{'q', '\n'};
        OSPLAY_CHECK(suite, ptys.write(session.pty, PtyEnd::master, input, 2).size == 2);
      } else if (wake == 2) {
        OSPLAY_CHECK(suite, interactive_child.signals.queue(signal_interrupt) ==
                               SignalStatus::success);
      }
      arch::test_ticks = 200;
      OSPLAY_CHECK(suite, resume_interactive_child_if_ready(session.frame));
      OSPLAY_CHECK(suite, process.pid == program);
      OSPLAY_CHECK(suite, parent.lineage.pid == session.shell);
      if (wake == 1) {
        // Ready input takes precedence over expiration and replays the call.
        OSPLAY_CHECK(suite, session.frame.mepc == 0x81001000);
        OSPLAY_CHECK(suite, session.frame.x[10] == argument);
      } else {
        OSPLAY_CHECK(suite, session.frame.mepc == 0x81001004);
        OSPLAY_CHECK(suite,
                    session.frame.x[10] ==
                        (wake == 2 ? static_cast<u32>(error(Errno::interrupted)) : 0));
      }
      if (select) {
        for (u32 i = 0; i < 3; ++i) {
          OSPLAY_CHECK(suite, sets[i] == (wake == 0 ? 0u : 1u));
        }
      } else {
        const auto* pollfd = reinterpret_cast<const Pollfd32*>(user_begin);
        OSPLAY_CHECK(suite, pollfd->descriptor == 0 && pollfd->events == 1 &&
                               pollfd->returned_events == 0);
      }
      OSPLAY_CHECK(suite, resume_after_child_exit(session.frame, 0));
      OSPLAY_CHECK(suite, process.pid == session.shell);
      OSPLAY_CHECK(suite, waitid(1, program, user_begin, 4) == 0);
      OSPLAY_CHECK(suite, resume_after_child_exit(session.frame, 0));
      OSPLAY_CHECK(suite, process.pid == session.server);
      OSPLAY_CHECK(suite, resume_after_child_exit(session.frame, 0));
      session.finish(suite);
    }
  }
  munmap(memory, arena_end - user_begin);
  return suite.finish();
}
