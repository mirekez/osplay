#define _GNU_SOURCE
#include "pthread_impl.h"
#include <setjmp.h>
uintptr_t __extreme_thread_pointer;
static struct pthread main_thread;
static size_t auxv[2];
extern char **environ;
void extreme_libc_init(char **environment) {
  main_thread.self = main_thread.prev = main_thread.next = &main_thread;
  main_thread.tid = 1;
  main_thread.locale = &libc.global_locale;
  __extreme_thread_pointer = (uintptr_t)(&main_thread + 1);
  libc.page_size = 4096;
  libc.auxv = auxv;
  environ = environment;
}
void __extreme_sigsetjmp_prepare(struct __jmp_buf_tag *context, int save) {
  context->__fl = save;
  if (save) {
    __syscall(SYS_rt_sigprocmask, SIG_BLOCK, 0, context->__ss, _NSIG / 8);
  }
}
void __extreme_siglongjmp_prepare(struct __jmp_buf_tag *context) {
  if (context->__fl) {
    __syscall(SYS_rt_sigprocmask, SIG_SETMASK, context->__ss, 0, _NSIG / 8);
  }
}
// This profile has one userspace thread. Blocking syscalls yield through the
// kernel process scheduler; POSIX pthread creation/cancellation is unavailable.
long __syscall_cp_c(long n, long a, long b, long c, long d, long e, long f) {
  return __extreme_syscall(n, a, b, c, d, e, f);
}

void __extreme_longjmp_prepare(void) { __extreme_syscall(4095, 0, 0, 0, 0, 0, 0); }
