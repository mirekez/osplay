#pragma once

#include <osplay/base.hpp>
#ifdef OSPLAY_EXTREME_USERSPACE
#include <context/context.h>
#endif

namespace osplay {

struct
#ifdef OSPLAY_RV64
alignas(16)
#endif
TrapFrame {
  uword x[32];
  uword mepc;
  uword mstatus;
  uword mcause;
  uword mtval;
#ifdef OSPLAY_RV64
  u64 fp[32];
  uword fcsr;
#endif
#ifdef OSPLAY_EXTREME_USERSPACE
  ExtremeContext context;
  bool extreme_start;
  bool extreme_retry;
  u32 extreme_signal_handler;
  u32 extreme_signal_number;
#endif
};

#ifdef OSPLAY_RV64
static_assert(sizeof(TrapFrame) == 560);
#elif !defined(OSPLAY_EXTREME_USERSPACE)
static_assert(sizeof(TrapFrame) == 36 * sizeof(u32));
#endif

struct Scheduler {
  u32 next_entry{};
  u32 next_stack{};
  u32 timer_traps{};
  u32 user_preemptions{};
  u32 contract_violations{};
  bool start_pending{};

  constexpr void start_next_on_timer(u32 entry, u32 stack) {
    next_entry = entry;
    next_stack = stack;
    start_pending = true;
  }

  constexpr void on_timer(TrapFrame& frame, bool kernel_mie_clear) {
    ++timer_traps;
    constexpr u32 mstatus_mpp = 3u << 11;
    if (!kernel_mie_clear || (frame.mstatus & mstatus_mpp) != 0) {
      ++contract_violations;
      return;
    }

    ++user_preemptions;
    if (!start_pending) {
      return;
    }

    for (auto& value : frame.x) {
      value = 0;
    }
    frame.x[2] = next_stack;
    frame.mepc = next_entry;
    start_pending = false;
  }
};

}  // namespace osplay
