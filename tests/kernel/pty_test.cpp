#include <osplay/process/pty.hpp>

#include <support/test.hpp>

int main() {
  using namespace osplay::process_model;
  osplay::test::Suite suite{"kernel/pty"};
  PtyTable<2, 8> ptys;

  auto master = ptys.open_master();
  OSPLAY_CHECK(suite, master.status == PtyStatus::success);
  OSPLAY_CHECK(suite, ptys.used() == 1);
  OSPLAY_CHECK(suite, ptys.open_slave(master.handle.slot).status ==
                         PtyStatus::locked);
  OSPLAY_CHECK(suite, ptys.set_locked(master.handle, false) ==
                         PtyStatus::success);
  auto slave = ptys.open_slave(master.handle.slot);
  OSPLAY_CHECK(suite, slave.status == PtyStatus::success);
  OSPLAY_CHECK(suite, slave.handle == master.handle);
  OSPLAY_CHECK(suite, ptys.active(master.handle.slot));
  OSPLAY_CHECK(suite, ptys.mode(master.handle.slot) == 0666);
  OSPLAY_CHECK(suite, ptys.set_mode(master.handle.slot, 010622) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, ptys.mode(master.handle.slot) == 0622);

  // The transport tests deliberately use noncanonical input without echo.
  TermiosState transport = *ptys.termios(master.handle);
  transport.local_flags = 0;
  OSPLAY_CHECK(suite, ptys.set_termios(master.handle, transport) == PtyStatus::success);

  const osplay::u8 input[] = {'a', 'b', 'c', 'd', 'e', 'f'};
  osplay::u8 output[8]{};
  OSPLAY_CHECK(suite, ptys.write(master.handle, PtyEnd::master, input,
                                sizeof(input)).size == sizeof(input));
  OSPLAY_CHECK(suite, ptys.read(slave.handle, PtyEnd::slave, output, 4).size ==
                         4);
  OSPLAY_CHECK(suite, output[0] == 'a' && output[3] == 'd');
  const osplay::u8 wrap[] = {'g', 'h', 'i', 'j', 'k', 'l'};
  OSPLAY_CHECK(suite,
              ptys.write(master.handle, PtyEnd::master, wrap, sizeof(wrap))
                      .size == 6);
  OSPLAY_CHECK(suite,
              ptys.read(slave.handle, PtyEnd::slave, output, sizeof(output))
                      .size == sizeof(output));
  OSPLAY_CHECK(suite, output[0] == 'e' && output[1] == 'f');
  OSPLAY_CHECK(suite, output[2] == 'g' && output[7] == 'l');

  const osplay::u8 reply[] = {'O', 'K'};
  OSPLAY_CHECK(suite,
              ptys.write(slave.handle, PtyEnd::slave, reply, sizeof(reply))
                      .size == 2);
  OSPLAY_CHECK(suite, ptys.readable(master.handle, PtyEnd::master));
  OSPLAY_CHECK(suite,
              ptys.read(master.handle, PtyEnd::master, output, 2).size == 2);
  OSPLAY_CHECK(suite, output[0] == 'O' && output[1] == 'K');

  // The slave output discipline starts in cooked mode. OPOST | ONLCR maps
  // each line feed to CR-LF so a real terminal returns to column zero. The
  // reported write count remains the number of caller bytes consumed.
  const osplay::u8 lines[] = {'a', '\n', 'b'};
  OSPLAY_CHECK(suite,
              ptys.write(slave.handle, PtyEnd::slave, lines, sizeof(lines))
                      .size == sizeof(lines));
  OSPLAY_CHECK(suite,
              ptys.read(master.handle, PtyEnd::master, output,
                        sizeof(output)).size == 4);
  OSPLAY_CHECK(suite, output[0] == 'a' && output[1] == '\r' &&
                         output[2] == '\n' && output[3] == 'b');

  // A CR-LF expansion is atomic at the ring boundary. With only one slot
  // left, the newline remains unconsumed and a retry after draining emits
  // exactly one CR-LF pair.
  const osplay::u8 almost_full[] = {'0', '1', '2', '3', '4', '5', '6'};
  OSPLAY_CHECK(
      suite,
      ptys.write(slave.handle, PtyEnd::slave, almost_full,
                 sizeof(almost_full)).size == sizeof(almost_full));
  const osplay::u8 newline[] = {'\n'};
  OSPLAY_CHECK(suite,
              ptys.write(slave.handle, PtyEnd::slave, newline,
                         sizeof(newline)).status == PtyStatus::would_block);
  OSPLAY_CHECK(suite,
              ptys.read(master.handle, PtyEnd::master, output,
                        sizeof(output)).size == sizeof(almost_full));
  OSPLAY_CHECK(suite,
              ptys.write(slave.handle, PtyEnd::slave, newline,
                         sizeof(newline)).size == sizeof(newline));
  OSPLAY_CHECK(suite,
              ptys.read(master.handle, PtyEnd::master, output,
                        sizeof(output)).size == 2);
  OSPLAY_CHECK(suite, output[0] == '\r' && output[1] == '\n');

  // Clearing either OPOST or ONLCR disables conversion.
  TermiosState unprocessed = *ptys.termios(master.handle);
  unprocessed.output_flags &= ~0x00000001u;
  OSPLAY_CHECK(suite, ptys.set_termios(slave.handle, unprocessed) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite,
              ptys.write(slave.handle, PtyEnd::slave, newline,
                         sizeof(newline)).size == sizeof(newline));
  OSPLAY_CHECK(suite,
              ptys.read(master.handle, PtyEnd::master, output,
                        sizeof(output)).size == 1);
  OSPLAY_CHECK(suite, output[0] == '\n');
  unprocessed.output_flags = 0x00000001;  // OPOST without ONLCR
  OSPLAY_CHECK(suite, ptys.set_termios(slave.handle, unprocessed) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite,
              ptys.write(slave.handle, PtyEnd::slave, newline,
                         sizeof(newline)).size == sizeof(newline));
  OSPLAY_CHECK(suite,
              ptys.read(master.handle, PtyEnd::master, output,
                        sizeof(output)).size == 1);
  OSPLAY_CHECK(suite, output[0] == '\n');

  TermiosState raw = *ptys.termios(master.handle);
  raw.output_flags = 0x00000005;
  raw.local_flags = 0;
  OSPLAY_CHECK(suite, ptys.set_termios(slave.handle, raw) == PtyStatus::success);
  OSPLAY_CHECK(suite, *ptys.termios(master.handle) == raw);
  bool changed = false;
  WindowSize window{40, 132, 640, 480};
  OSPLAY_CHECK(suite, ptys.set_window(master.handle, window, &changed) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, changed);
  OSPLAY_CHECK(suite, *ptys.window(slave.handle) == window);
  OSPLAY_CHECK(suite, ptys.set_window(slave.handle, window, &changed) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, !changed);

  // Only a session leader acquires a controlling slave. Foreground state is
  // shared by both endpoints and can only be changed by that session.
  OSPLAY_CHECK(suite, ptys.acquire_controlling_terminal(
                         slave.handle, 7, 7, 8) ==
                         PtyStatus::permission_denied);
  OSPLAY_CHECK(suite, ptys.acquire_controlling_terminal(
                         slave.handle, 7, 7, 7) == PtyStatus::success);
  OSPLAY_CHECK(suite, ptys.session(master.handle) == 7);
  OSPLAY_CHECK(suite, ptys.foreground_group(master.handle) == 7);
  auto controlling = ptys.open_controlling_slave(7);
  OSPLAY_CHECK(suite, controlling.status == PtyStatus::success);
  OSPLAY_CHECK(suite, controlling.handle == slave.handle);
  OSPLAY_CHECK(suite, ptys.open_controlling_slave(8).status ==
                         PtyStatus::bad_handle);
  OSPLAY_CHECK(suite, ptys.release(controlling.handle, PtyEnd::slave) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, ptys.set_foreground_group(slave.handle, 8, 9) ==
                         PtyStatus::permission_denied);
  OSPLAY_CHECK(suite, ptys.set_foreground_group(slave.handle, 7, 9) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, ptys.foreground_group(master.handle) == 9);

  OSPLAY_CHECK(suite, ptys.retain(slave.handle, PtyEnd::slave) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, ptys.release(slave.handle, PtyEnd::slave) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, ptys.release(master.handle, PtyEnd::master) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, ptys.read(slave.handle, PtyEnd::slave, output, 1).status ==
                         PtyStatus::end_of_file);
  OSPLAY_CHECK(suite, ptys.write(slave.handle, PtyEnd::slave, reply, 1).status ==
                         PtyStatus::io_error);
  OSPLAY_CHECK(suite, ptys.release(slave.handle, PtyEnd::slave) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, ptys.used() == 0);
  OSPLAY_CHECK(suite, !ptys.active(master.handle.slot));
  OSPLAY_CHECK(suite, ptys.mode(master.handle.slot) == 0);
  OSPLAY_CHECK(suite, ptys.set_mode(master.handle.slot, 0600) ==
                         PtyStatus::bad_handle);

  auto reused = ptys.open_master();
  OSPLAY_CHECK(suite, reused.handle.slot == master.handle.slot);
  OSPLAY_CHECK(suite, reused.handle.generation != master.handle.generation);
  OSPLAY_CHECK(suite, ptys.mode(reused.handle.slot) == 0666);
  OSPLAY_CHECK(suite, ptys.set_locked(master.handle, false) ==
                         PtyStatus::bad_handle);
  OSPLAY_CHECK(suite, ptys.open_master().status == PtyStatus::success);
  OSPLAY_CHECK(suite, ptys.open_master().status == PtyStatus::no_space);

  // Closing the final slave must not discard bytes already written to the
  // master. A PTY server commonly receives SIGCHLD before it drains the last
  // command output, and Linux permits that buffered output to be read first.
  PtyTable<1, 8> drain_ptys;
  const auto drain_master = drain_ptys.open_master();
  OSPLAY_CHECK(suite, drain_ptys.set_locked(drain_master.handle, false) ==
                         PtyStatus::success);
  const auto drain_slave =
      drain_ptys.open_slave(drain_master.handle.slot);
  const osplay::u8 final_output[] = {'d', 'o', 'n', 'e'};
  OSPLAY_CHECK(suite,
              drain_ptys.write(drain_slave.handle, PtyEnd::slave,
                               final_output, sizeof(final_output)).size ==
                  sizeof(final_output));
  OSPLAY_CHECK(suite,
              drain_ptys.release(drain_slave.handle, PtyEnd::slave) ==
                  PtyStatus::success);
  OSPLAY_CHECK(suite,
              drain_ptys.read(drain_master.handle, PtyEnd::master, output,
                              sizeof(output)).size == sizeof(final_output));
  OSPLAY_CHECK(suite, output[0] == 'd' && output[3] == 'e');
  OSPLAY_CHECK(suite,
              drain_ptys.read(drain_master.handle, PtyEnd::master, output, 1)
                      .status == PtyStatus::end_of_file);

  // Serialized interactive handoff: the shell writes a prompt and blocks on
  // its slave, the server drains the master and supplies a command, then the
  // shell becomes readable and can answer before closing. This is the exact
  // readiness sequence used by the flat fork adapter for an SSH login shell.
  PtyTable<1, 32> interactive_ptys;
  const auto interactive_master = interactive_ptys.open_master();
  OSPLAY_CHECK(suite,
              interactive_ptys.set_locked(interactive_master.handle, false) ==
                  PtyStatus::success);
  const auto interactive_slave =
      interactive_ptys.open_slave(interactive_master.handle.slot);
  const osplay::u8 prompt[] = {'/', ' ', '#', ' '};
  OSPLAY_CHECK(
      suite,
      interactive_ptys
              .write(interactive_slave.handle, PtyEnd::slave, prompt,
                     sizeof(prompt))
              .size == sizeof(prompt));
  OSPLAY_CHECK(suite,
              !interactive_ptys.readable(interactive_slave.handle,
                                         PtyEnd::slave));
  OSPLAY_CHECK(suite,
              interactive_ptys.read(interactive_slave.handle, PtyEnd::slave,
                                    output, sizeof(output)).status ==
                  PtyStatus::would_block);
  OSPLAY_CHECK(suite,
              interactive_ptys.read(interactive_master.handle, PtyEnd::master,
                                    output, sizeof(output)).size ==
                  sizeof(prompt));
  OSPLAY_CHECK(suite, output[0] == '/' && output[3] == ' ');
  const osplay::u8 command[] = {'e', 'x', 'i', 't', '\n'};
  OSPLAY_CHECK(
      suite,
      interactive_ptys
              .write(interactive_master.handle, PtyEnd::master, command,
                     sizeof(command))
              .size == sizeof(command));
  OSPLAY_CHECK(suite,
              interactive_ptys.readable(interactive_slave.handle,
                                         PtyEnd::slave));
  OSPLAY_CHECK(suite,
              interactive_ptys.read(interactive_slave.handle, PtyEnd::slave,
                                    output, sizeof(output)).size ==
                  sizeof(command));
  OSPLAY_CHECK(suite, output[0] == 'e' && output[4] == '\n');
  OSPLAY_CHECK(suite,
              interactive_ptys.read(interactive_master.handle, PtyEnd::master,
                                    output, sizeof(output)).size == 6);
  OSPLAY_CHECK(suite, output[0] == 'e' && output[4] == '\r' && output[5] == '\n');
  OSPLAY_CHECK(suite,
              interactive_ptys.release(interactive_slave.handle,
                                       PtyEnd::slave) == PtyStatus::success);
  OSPLAY_CHECK(suite,
              interactive_ptys.read(interactive_master.handle, PtyEnd::master,
                                    output, 1).status ==
                  PtyStatus::end_of_file);

  // A PTY master must apply the slave's ISIG control characters instead of
  // passing them to the foreground program as ordinary bytes. Signal input is
  // consumed, flushes queued input/output unless NOFLSH is set, and uses
  // ECHOCTL's familiar caret notation when requested.
  PtyTable<1, 32> signal_ptys;
  const auto signal_master = signal_ptys.open_master();
  OSPLAY_CHECK(suite,
              signal_ptys.set_locked(signal_master.handle, false) ==
                  PtyStatus::success);
  const auto signal_slave =
      signal_ptys.open_slave(signal_master.handle.slot);
  TermiosState signal_termios = *signal_ptys.termios(signal_slave.handle);
  signal_termios.local_flags |= 0x00000200;  // ECHOCTL
  OSPLAY_CHECK(suite,
              signal_ptys.set_termios(signal_slave.handle, signal_termios) ==
                  PtyStatus::success);
  const osplay::u8 stale_input[]{'o', 'l', 'd'};
  OSPLAY_CHECK(suite,
              signal_ptys
                      .write(signal_master.handle, PtyEnd::master,
                             stale_input, sizeof(stale_input))
                      .size == sizeof(stale_input));
  const osplay::u8 interrupt[]{3};
  const auto interrupted = signal_ptys.write(
      signal_master.handle, PtyEnd::master, interrupt, sizeof(interrupt));
  OSPLAY_CHECK(suite, interrupted.status == PtyStatus::success);
  OSPLAY_CHECK(suite, interrupted.size == sizeof(interrupt));
  OSPLAY_CHECK(suite,
              interrupted.generated_signals ==
                  SignalState::bit(signal_interrupt));
  OSPLAY_CHECK(suite,
              signal_ptys.read(signal_slave.handle, PtyEnd::slave, output, 1)
                      .status == PtyStatus::would_block);
  OSPLAY_CHECK(suite,
              signal_ptys.read(signal_master.handle, PtyEnd::master, output,
                               sizeof(output))
                      .size == 2);
  OSPLAY_CHECK(suite, output[0] == '^' && output[1] == 'C');

  // NOFLSH preserves bytes already waiting for the slave. VQUIT and VSUSP
  // generate their respective process-group signals without entering that
  // input queue.
  signal_termios.local_flags |= 0x00000080;  // NOFLSH
  signal_termios.local_flags &= ~0x2u;  // Noncanonical signal transport
  OSPLAY_CHECK(suite,
              signal_ptys.set_termios(signal_slave.handle, signal_termios) ==
                  PtyStatus::success);
  const osplay::u8 preserved[]{'x'};
  OSPLAY_CHECK(suite,
              signal_ptys
                      .write(signal_master.handle, PtyEnd::master, preserved,
                             sizeof(preserved))
                      .size == sizeof(preserved));
  OSPLAY_CHECK(suite,
              signal_ptys
                      .write(signal_master.handle, PtyEnd::master, interrupt,
                             sizeof(interrupt))
                      .generated_signals ==
                  SignalState::bit(signal_interrupt));
  OSPLAY_CHECK(suite,
              signal_ptys.read(signal_slave.handle, PtyEnd::slave, output, 1)
                      .size == 1);
  OSPLAY_CHECK(suite, output[0] == 'x');
  const osplay::u8 quit[]{28};
  OSPLAY_CHECK(suite,
              signal_ptys
                      .write(signal_master.handle, PtyEnd::master, quit,
                             sizeof(quit))
                      .generated_signals == SignalState::bit(signal_quit));
  const osplay::u8 suspend[]{26};
  OSPLAY_CHECK(
      suite,
      signal_ptys
              .write(signal_master.handle, PtyEnd::master, suspend,
                     sizeof(suspend))
              .generated_signals == SignalState::bit(signal_tty_stop));

  // Raw mode and an explicitly disabled control-character slot pass the byte
  // through unchanged and generate no signal.
  TermiosState raw_signal_termios = signal_termios;
  raw_signal_termios.local_flags = 0;
  OSPLAY_CHECK(
      suite,
      signal_ptys.set_termios(signal_slave.handle, raw_signal_termios) ==
          PtyStatus::success);
  const auto raw_interrupt = signal_ptys.write(
      signal_master.handle, PtyEnd::master, interrupt, sizeof(interrupt));
  OSPLAY_CHECK(suite, raw_interrupt.generated_signals == 0);
  OSPLAY_CHECK(suite,
              signal_ptys.read(signal_slave.handle, PtyEnd::slave, output, 1)
                      .size == 1);
  OSPLAY_CHECK(suite, output[0] == 3);
  TermiosState disabled_interrupt = signal_termios;
  disabled_interrupt.control[0] = 0;
  OSPLAY_CHECK(
      suite,
      signal_ptys.set_termios(signal_slave.handle, disabled_interrupt) ==
          PtyStatus::success);
  const osplay::u8 nul[]{0};
  const auto disabled_result = signal_ptys.write(
      signal_master.handle, PtyEnd::master, nul, sizeof(nul));
  OSPLAY_CHECK(suite, disabled_result.generated_signals == 0);
  OSPLAY_CHECK(suite,
              signal_ptys.read(signal_slave.handle, PtyEnd::slave, output, 1)
                      .size == 1);
  OSPLAY_CHECK(suite, output[0] == 0);

  SignalState pending_signal;
  OSPLAY_CHECK(suite, !pending_signal.has_deliverable());
  OSPLAY_CHECK(suite, pending_signal.queue(signal_interrupt) ==
                         SignalStatus::success);
  OSPLAY_CHECK(suite, pending_signal.has_deliverable());
  OSPLAY_CHECK(suite,
              pending_signal.change_mask(
                  SignalMaskOperation::block,
                  SignalState::bit(signal_interrupt)) ==
                  SignalStatus::success);
  OSPLAY_CHECK(suite, !pending_signal.has_deliverable());
  OSPLAY_CHECK(suite,
              pending_signal.change_mask(
                  SignalMaskOperation::unblock,
                  SignalState::bit(signal_interrupt)) ==
                  SignalStatus::success);
  OSPLAY_CHECK(suite, pending_signal.has_deliverable());

  // Ownership is shared by a PTY's endpoints and reset when its slot is reused.
  PtyTable<1, 8> owned;
  const auto first_owner = owned.open_master();
  const auto number = first_owner.handle.slot;
  OSPLAY_CHECK(suite, owned.uid(number) == 0 && owned.gid(number) == 0);
  OSPLAY_CHECK(suite, owned.set_owner(number, 1000, 100) == PtyStatus::success);
  OSPLAY_CHECK(suite, owned.set_owner(number, ~osplay::u32{0}, 200) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, owned.uid(number) == 1000 && owned.gid(number) == 200);
  OSPLAY_CHECK(suite, owned.set_owner(number, 3000, ~osplay::u32{0}) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, owned.uid(number) == 3000 && owned.gid(number) == 200);
  OSPLAY_CHECK(suite, owned.release(first_owner.handle, PtyEnd::master) ==
                         PtyStatus::success);
  OSPLAY_CHECK(suite, owned.set_owner(number, 0, 0) == PtyStatus::bad_handle);
  const auto second_owner = owned.open_master();
  OSPLAY_CHECK(suite, second_owner.status == PtyStatus::success);
  OSPLAY_CHECK(suite, owned.uid(number) == 0 && owned.gid(number) == 0);

  return suite.finish();
}
