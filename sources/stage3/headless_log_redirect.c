#define _GNU_SOURCE
#include <stdarg.h>
#include <signal.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

static void crash_handler(int sig, siginfo_t *info, void *context) {
  void *pc = 0;
  void *lr = 0;
#if defined(__arm__)
  ucontext_t *uc = (ucontext_t *)context;
  pc = (void *)uc->uc_mcontext.arm_pc;
  lr = (void *)uc->uc_mcontext.arm_lr;
#elif defined(__aarch64__)
  ucontext_t *uc = (ucontext_t *)context;
  pc = (void *)uc->uc_mcontext.pc;
  lr = (void *)uc->uc_mcontext.regs[30];
#endif
  Dl_info symbol = {0};
  int found = pc ? dladdr(pc, &symbol) : 0;
  Dl_info caller = {0};
  int caller_found = lr ? dladdr(lr, &caller) : 0;
  dprintf(2, "HEADLESS_CRASH signal=%d fault=%p pc=%p object=%s symbol=%s lr=%p caller=%s caller_symbol=%s caller_base=%p\n",
          sig, info ? info->si_addr : 0, pc,
          found && symbol.dli_fname ? symbol.dli_fname : "?",
          found && symbol.dli_sname ? symbol.dli_sname : "?", lr,
          caller_found && caller.dli_fname ? caller.dli_fname : "?",
          caller_found && caller.dli_sname ? caller.dli_sname : "?",
          caller_found ? caller.dli_fbase : 0);
  signal(sig, SIG_DFL);
  raise(sig);
  _exit(128 + sig);
}

__attribute__((constructor)) static void install_crash_handler(void) {
  struct sigaction action;
  memset(&action, 0, sizeof(action));
  action.sa_sigaction = crash_handler;
  action.sa_flags = SA_SIGINFO | SA_RESETHAND;
  sigemptyset(&action.sa_mask);
  sigaction(SIGSEGV, &action, 0);
  sigaction(SIGABRT, &action, 0);
}

static int write_line(int buf, int prio, const char *tag, const char *text) {
  /* Production sessions must not write every DispSync/evdev debug tick to
   * the Kindle flash. Retain informational messages and all warnings/errors. */
  if (prio < 4) return 0;
  int n = dprintf(2, "ANDROID_LOG buf=%d prio=%d tag=%s %s\n", buf, prio,
                  tag ? tag : "?", text ? text : "");
  return n < 0 ? n : 0;
}

int __android_log_buf_write(int buf, int prio, const char *tag,
                            const char *text) {
  return write_line(buf, prio, tag, text);
}

int __android_log_write(int prio, const char *tag, const char *text) {
  return write_line(0, prio, tag, text);
}

int __android_log_buf_print(int buf, int prio, const char *tag,
                            const char *fmt, ...) {
  char message[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(message, sizeof(message), fmt ? fmt : "", ap);
  va_end(ap);
  return write_line(buf, prio, tag, message);
}

int __android_log_print(int prio, const char *tag, const char *fmt, ...) {
  char message[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(message, sizeof(message), fmt ? fmt : "", ap);
  va_end(ap);
  return write_line(0, prio, tag, message);
}

void __android_log_assert(const char *condition, const char *tag,
                          const char *fmt, ...) {
  char message[2048];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(message, sizeof(message), fmt ? fmt : "assertion failed", ap);
  va_end(ap);
  dprintf(2, "ANDROID_FATAL tag=%s condition=%s %s\n", tag ? tag : "?",
          condition ? condition : "?", message);
  abort();
}
