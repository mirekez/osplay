#pragma once

#include <osplay/base.hpp>

namespace osplay::arch {

inline constexpr uword interrupt_bit = uword{1} << (sizeof(uword)*8-1);
inline constexpr uword scheduler_timer_cause = interrupt_bit | 7;
inline constexpr uword tribe_user_timer_cause = interrupt_bit | 5;

void start_scheduler_timer();
void rearm_scheduler_timer();
void set_network_timer_waiting(bool waiting);
[[nodiscard]] u64 time_ticks();

}  // namespace osplay::arch
