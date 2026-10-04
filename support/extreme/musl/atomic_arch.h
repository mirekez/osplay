// This libc instance belongs to one cooperatively scheduled userspace core.
// Kernel task cores do not share its pthread/allocator state. Thread creation
// is rejected by the userspace syscall adapter; these are not SMP atomics.
#define a_barrier a_barrier
static inline void a_barrier(void) { __asm__ __volatile__("" ::: "memory"); }
#define a_cas a_cas
static inline int a_cas(volatile int *p, int expected, int desired) {
  int value = *p;
  if (value == expected) {
    *p = desired;
  }
  a_barrier();
  return value;
}
