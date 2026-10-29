// Absturzprotokoll für den Gast: PC, LR, Fehleradresse, Rahmenkette (dladdr). Gebaut wie xlat/build.sh gegen die Image-libSystem.
#define _XOPEN_SOURCE 600
#define _DARWIN_C_SOURCE
// Absturz-Protokoll: DYLD_INSERT_LIBRARIES=./libcrash.dylib ./prog  -> PC, LR, Fehleradresse und Rahmenkette mit Bildnamen.
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <dlfcn.h>
#include <ucontext.h>
static void sym(const char *tag, uintptr_t a) {
  Dl_info i; char b[300];
  if (dladdr((void *)a, &i)) snprintf(b, sizeof b, "%s %#lx %s+%#lx (%s)\n", tag, (long)a, i.dli_sname ? i.dli_sname : "?", (long)(a - (uintptr_t)(i.dli_saddr ? i.dli_saddr : i.dli_fbase)), i.dli_fname);
  else snprintf(b, sizeof b, "%s %#lx\n", tag, (long)a);
  write(2, b, strlen(b));
}
static void h(int sig, siginfo_t *si, void *uc) {
  ucontext_t *u = uc; char b[100];
  snprintf(b, sizeof b, "CRASH sig %d addr %p\n", sig, si->si_addr); write(2, b, strlen(b));
  sym("pc", u->uc_mcontext->__ss.__pc); sym("lr", u->uc_mcontext->__ss.__lr);
  uintptr_t *fp = (uintptr_t *)u->uc_mcontext->__ss.__fp;
  for (int n = 0; fp && n < 12; n++) { sym("  bt", fp[1]); fp = (uintptr_t *)fp[0]; if ((uintptr_t)fp & 7) break; }
  _exit(139);
}
__attribute__((constructor)) static void init(void) {
  struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = h; sa.sa_flags = SA_SIGINFO;
  sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0); sigaction(SIGILL, &sa, 0); sigaction(SIGTRAP, &sa, 0); sigaction(SIGABRT, &sa, 0);
}
