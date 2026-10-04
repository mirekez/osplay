#include "pthread_impl.h"
#include "syscall.h"
static void *unmap_base;
static size_t unmap_size;
static _Alignas(EC_BUS_BYTES) unsigned char activation[16384];
extern unsigned __extreme_activation_size(void (*)(void));
extern _Noreturn void __extreme_context_enter(void (*)(void), void *);
static _Noreturn void do_unmap(void) {
  __syscall(SYS_munmap, unmap_base, unmap_size);
  __syscall(SYS_exit, 0);
  __builtin_trap();
}
_Noreturn void __unmapself(void *base, size_t size) {
  unmap_base = base;
  unmap_size = size;
  if (__extreme_activation_size(do_unmap) > sizeof(activation)) {
    __builtin_trap();
  }
  __extreme_context_enter(do_unmap, activation);
}
