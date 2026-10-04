extern uintptr_t __extreme_thread_pointer;
static inline uintptr_t __get_tp(void) { return __extreme_thread_pointer; }
#define TLS_ABOVE_TP
#define GAP_ABOVE_TP 0
#define DTP_OFFSET 0x800
#define MC_PC __gregs[0]
