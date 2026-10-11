#include <osplay/process/pty.hpp>
#include <support/test.hpp>

int main() {
  using namespace osplay;
  using namespace osplay::process_model;
  test::Suite suite{"kernel/pty_line"};
  PtyTable<1, 16> ptys;
  const auto master = ptys.open_master().handle;
  OSPLAY_CHECK(suite, ptys.set_locked(master, false) == PtyStatus::success);
  const auto slave = ptys.open_slave(master.slot).handle;
  u8 output[32]{};
  const auto type = [&](const char* text) {
    u32 size = 0;
    while (text[size]) ++size;
    return ptys.write(master, PtyEnd::master, reinterpret_cast<const u8*>(text), size);
  };
  const auto read_input = [&](u32 size = sizeof(output)) {
    return ptys.read(slave, PtyEnd::slave, output, size);
  };
  const auto read_echo = [&]() {
    return ptys.read(master, PtyEnd::master, output, sizeof(output));
  };

  // Real dbclient sends one character per packet and Enter as CR. The shell
  // must stay parked until Enter while the server can drain echoed input.
  OSPLAY_CHECK(suite, type("l").size == 1);
  OSPLAY_CHECK(suite, !ptys.readable(slave, PtyEnd::slave));
  OSPLAY_CHECK(suite, read_input().status == PtyStatus::would_block);
  OSPLAY_CHECK(suite, read_echo().size == 1 && output[0] == 'l');
  OSPLAY_CHECK(suite, type("s").size == 1);
  OSPLAY_CHECK(suite, !ptys.readable(slave, PtyEnd::slave));
  OSPLAY_CHECK(suite, read_echo().size == 1 && output[0] == 's');
  OSPLAY_CHECK(suite, type("\r").size == 1);
  OSPLAY_CHECK(suite, ptys.readable(slave, PtyEnd::slave));
  OSPLAY_CHECK(suite, read_echo().size == 2 && output[0] == '\r' && output[1] == '\n');
  OSPLAY_CHECK(suite, read_input(1).size == 1 && output[0] == 'l');
  OSPLAY_CHECK(suite, read_input().size == 2 && output[0] == 's' && output[1] == '\n');
  OSPLAY_CHECK(suite, !ptys.readable(slave, PtyEnd::slave));

  // Canonical reads stop at a record boundary even when commands arrive together.
  OSPLAY_CHECK(suite, type("ls\rls\r").size == 6);
  OSPLAY_CHECK(suite, read_input().size == 3 && output[2] == '\n');
  OSPLAY_CHECK(suite, read_input().size == 3 && output[2] == '\n');
  OSPLAY_CHECK(suite, read_echo().size == 8);

  OSPLAY_CHECK(suite, type("lx\177s\r").size == 5);
  OSPLAY_CHECK(suite, read_echo().size == 8 && output[2] == '\b');
  OSPLAY_CHECK(suite, read_input().size == 3 && output[0] == 'l' && output[1] == 's');
  OSPLAY_CHECK(suite, type("bad\025ok\r").size == 7);  // VKILL
  OSPLAY_CHECK(suite, read_input().size == 3 && output[0] == 'o' && output[1] == 'k');
  static_cast<void>(read_echo());

  // VEOF ends a partial line, and at the start of a line produces exactly one
  // zero-length read. It must not permanently close the slave or swallow data.
  OSPLAY_CHECK(suite, type("ab\004\004c\r").size == 6);
  OSPLAY_CHECK(suite, read_input().size == 2 && output[0] == 'a' && output[1] == 'b');
  const auto eof = read_input();
  OSPLAY_CHECK(suite, eof.status == PtyStatus::success && eof.size == 0);
  OSPLAY_CHECK(suite, read_input().size == 2 && output[0] == 'c' && output[1] == '\n');
  OSPLAY_CHECK(suite, read_input().status == PtyStatus::would_block);
  static_cast<void>(read_echo());

  // Overlong lines still accept a delimiter, and queued records apply
  // backpressure without losing or duplicating the retry's bytes.
  OSPLAY_CHECK(suite, type("abcdefghijklmnopqr\r").size == 19);
  OSPLAY_CHECK(suite, !ptys.writable(master, PtyEnd::master));
  OSPLAY_CHECK(suite, type("x\r").status == PtyStatus::would_block);
  OSPLAY_CHECK(suite,
              read_input().size == 16 && output[14] == 'o' && output[15] == '\n');
  static_cast<void>(read_echo());
  OSPLAY_CHECK(suite, type("x\r").size == 2);
  OSPLAY_CHECK(suite, read_input().size == 2 && output[0] == 'x');
  static_cast<void>(read_echo());

  // Empty EOF records consume metadata capacity even though there are no bytes.
  for (unsigned i = 0; i < 16; ++i) {
    OSPLAY_CHECK(suite, type("\004").size == 1);
  }
  OSPLAY_CHECK(suite, type("\004").status == PtyStatus::would_block);
  for (unsigned i = 0; i < 16; ++i) {
    const auto empty = read_input();
    OSPLAY_CHECK(suite, empty.status == PtyStatus::success && empty.size == 0);
  }
  OSPLAY_CHECK(suite, !ptys.readable(slave, PtyEnd::slave));

  // Switching to raw mode releases partial input; raw bytes are neither
  // echoed nor CR-translated when the corresponding flags are cleared.
  OSPLAY_CHECK(suite, type("raw").size == 3);
  OSPLAY_CHECK(suite, read_echo().size == 3);
  TermiosState raw{};
  raw.local_flags = raw.input_flags = raw.output_flags = 0;
  OSPLAY_CHECK(suite, ptys.set_termios(slave, raw) == PtyStatus::success);
  OSPLAY_CHECK(suite, read_input().size == 3 && output[0] == 'r');
  OSPLAY_CHECK(suite, type("\r").size == 1);
  OSPLAY_CHECK(suite, read_input().size == 1 && output[0] == '\r');
  OSPLAY_CHECK(suite, read_echo().status == PtyStatus::would_block);

  TermiosState cooked{};
  cooked.input_flags |= 0x80;  // IGNCR overrides ICRNL.
  OSPLAY_CHECK(suite, ptys.set_termios(slave, cooked) == PtyStatus::success);
  OSPLAY_CHECK(suite, type("a\r").size == 2);
  OSPLAY_CHECK(suite, !ptys.readable(slave, PtyEnd::slave));
  OSPLAY_CHECK(suite, type("\n").size == 1);
  OSPLAY_CHECK(suite, read_input().size == 2 && output[0] == 'a' && output[1] == '\n');
  static_cast<void>(read_echo());

  // Signals flush both a committed command and the unfinished next line.
  OSPLAY_CHECK(suite, type("a\nb").size == 3);
  OSPLAY_CHECK(suite,
              type("\003").generated_signals == SignalState::bit(signal_interrupt));
  OSPLAY_CHECK(suite, read_input().status == PtyStatus::would_block);
  OSPLAY_CHECK(suite, type("z").size == 1);
  OSPLAY_CHECK(suite, ptys.release(master, PtyEnd::master) == PtyStatus::success);
  OSPLAY_CHECK(suite, read_input().size == 1 && output[0] == 'z');
  OSPLAY_CHECK(suite, read_input().status == PtyStatus::end_of_file);
  return suite.finish();
}
