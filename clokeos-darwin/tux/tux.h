// tux – Linux-Programme auf ClokeOS (Darwin/XNU). Gemeinsame Deklarationen.
#ifndef TUX_H
#define TUX_H
#define _DARWIN_C_SOURCE 1
#include <stdint.h>
#include <stddef.h>
#include <sys/ucontext.h>
#include <limits.h>
#include <signal.h>

#if defined(__aarch64__)
#define TUX_ARM64 1
#define TUX_EM 183
#define TUX_NAME "aarch64"
#elif defined(__x86_64__)
#define TUX_X86_64 1
#define TUX_EM 62
#define TUX_NAME "x86_64"
#else
#error "nur arm64 und x86_64"
#endif

// Seitengröße, die dem Linux-Programm gemeldet wird (AT_PAGESZ): die des Darwin-Kerns,
// 16 KiB auf arm64 und 4 KiB auf x86_64
extern size_t tux_page;

// Zustand des Linux-Prozesses
extern char tux_root[PATH_MAX];        // Wurzel, standardmäßig /TUX
extern char tux_cwd[PATH_MAX];         // Arbeitsordner aus Sicht von Linux
extern char tux_exe[PATH_MAX];         // Programm aus Sicht von Linux (für /proc/self/exe)
extern char tux_self[PATH_MAX];        // Pfad dieses Programms (für execve)
extern int tux_trace;

// Register der angehaltenen Stelle, an der das Linux-Programm einen Systemaufruf gemacht hat
typedef struct tux_regs {
	uint64_t nr, a[6];     // Nummer und Argumente (Linux-ABI)
	uint64_t *ret;         // hierhin kommt das Ergebnis
	uint64_t *sp, *pc;     // Stapelzeiger und Befehlszähler des Aufrufers (änderbar, z. B. clone)
	void *uc;              // Darwin-Kontext
} tux_regs;

long tux_syscall(tux_regs *r);
void tux_sys_init(void);

// Thread-Zeiger des Linux-Programms (TLS), pro Thread
void tux_set_tp(uint64_t tp);
uint64_t tux_get_tp(void);

// Signale
void tux_signals_init(void);
long tux_sigaction(long sig, const void *act, void *old, size_t size);
long tux_sigprocmask(long how, const uint64_t *set, uint64_t *old, size_t size);
int tux_sig_l2d(int s);
int tux_sig_d2l(int s);
void tux_guest_to_host_sigset(uint64_t l, sigset_t *d);

// Pfade
int tux_resolve(int dirfd, const char *path, int follow, char *host, char *guest);
const char *tux_errstr(int e);

// Fehlernummern Darwin -> Linux
int tux_errno(int e);

// ELF laden und starten
int tux_exec(const char *guest_path, char **argv, char **envp, int depth);
void tux_patch_range(void *addr, size_t len);

#endif
