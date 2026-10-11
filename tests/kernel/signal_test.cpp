#include <osplay/process/signal.hpp>

#include <support/test.hpp>

int main() {
  using namespace osplay::process_model;
  osplay::test::Suite suite{"kernel/signal"};
  SignalState signals;

  OSPLAY_CHECK(suite, !SignalState::valid(0));
  OSPLAY_CHECK(suite, SignalState::valid(1));
  OSPLAY_CHECK(suite, SignalState::valid(64));
  OSPLAY_CHECK(suite, signals.queue(65) == SignalStatus::invalid_signal);
  OSPLAY_CHECK(suite,
              signals.set_action(signal_kill, {}) == SignalStatus::uncatchable);
  OSPLAY_CHECK(suite,
              signals.set_action(signal_stop, {}) == SignalStatus::uncatchable);

  SignalAction action{0x81234000, 0x10000000,
                      SignalState::bit(2) | SignalState::bit(signal_kill)};
  SignalAction old{};
  OSPLAY_CHECK(suite, signals.set_action(10, action, &old) ==
                         SignalStatus::success);
  OSPLAY_CHECK(suite, old == SignalAction{});
  OSPLAY_CHECK(suite, signals.action(10).handler == action.handler);
  OSPLAY_CHECK(suite, (signals.action(10).mask &
                      SignalState::bit(signal_kill)) == 0);

  osplay::u64 previous = ~osplay::u64{0};
  const auto blocked = SignalState::bit(2) | SignalState::bit(10) |
                       SignalState::bit(signal_kill) |
                       SignalState::bit(signal_stop);
  OSPLAY_CHECK(suite,
              signals.change_mask(SignalMaskOperation::block, blocked,
                                  &previous) == SignalStatus::success);
  OSPLAY_CHECK(suite, previous == 0);
  OSPLAY_CHECK(suite,
              (signals.blocked() & SignalState::bit(signal_kill)) == 0);
  OSPLAY_CHECK(suite,
              (signals.blocked() & SignalState::bit(signal_stop)) == 0);

  // Standard signals coalesce while blocked; an unblocked lower signal is
  // delivered first, and unblocking makes the coalesced signal deliverable.
  OSPLAY_CHECK(suite, signals.queue(10) == SignalStatus::success);
  OSPLAY_CHECK(suite, signals.queue(10) == SignalStatus::success);
  OSPLAY_CHECK(suite, signals.queue(3) == SignalStatus::success);
  OSPLAY_CHECK(suite, signals.next() == 3);
  OSPLAY_CHECK(suite, signals.next() == 0);
  OSPLAY_CHECK(suite,
              signals.change_mask(SignalMaskOperation::unblock,
                                  SignalState::bit(10)) ==
                  SignalStatus::success);
  OSPLAY_CHECK(suite, signals.next() == 10);
  OSPLAY_CHECK(suite, signals.next() == 0);

  OSPLAY_CHECK(suite, SignalState::default_for(signal_child) ==
                         SignalDefault::ignore);
  OSPLAY_CHECK(suite, SignalState::default_for(signal_window_change) ==
                         SignalDefault::ignore);
  OSPLAY_CHECK(suite, SignalState::default_for(signal_continue) ==
                         SignalDefault::resume);
  OSPLAY_CHECK(suite, SignalState::default_for(signal_tty_stop) ==
                         SignalDefault::stop);
  OSPLAY_CHECK(suite,
              SignalState::default_for(signal_kill) == SignalDefault::terminate);

  OSPLAY_CHECK(suite,
              signals.change_mask(SignalMaskOperation::set,
                                  SignalState::bit(12), &previous) ==
                  SignalStatus::success);
  OSPLAY_CHECK(suite, (previous & SignalState::bit(2)) != 0);
  OSPLAY_CHECK(suite, signals.blocked() == SignalState::bit(12));

  return suite.finish();
}
