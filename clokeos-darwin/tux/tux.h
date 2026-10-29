// tux – Linux-Programme auf ClokeOS (Darwin/XNU). Gemeinsame Deklarationen.
#ifndef TUX_H
#define TUX_H
#define _DARWIN_C_SOURCE 1
#include <stdint.h>
#include <stddef.h>
#include <sys/ucontext.h>
#include <limits.h>
#include <signal.h>
#include <time.h>

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
// mmap für das Linux-Programm: auf x86_64 ohne Adresswunsch unterhalb von 4 GiB (der FS-Zeiger geht nur über einen
// LDT-Eintrag mit 32-Bit-Basis)
void *tux_mmap(void *want, size_t len, int prot, int flags);
long tux_arch_prctl(tux_regs *r, long code, uint64_t addr);
void tux_sys_init(void);

// Threads (thr.c): ein Linux-Thread ist ein Darwin-pthread
#include <pthread.h>
typedef struct tux_thr {
	int tid;
	pthread_t pt;
	uint64_t clear_tid;          // CLONE_CHILD_CLEARTID / set_tid_address
	void *host_sp;               // Stapel des pthreads (zum Beenden, wenn der Gaststapel schon weg sein kann)
	int ldt_idx;                 // x86_64: LDT-Eintrag für FS
	uint64_t ldt_cur;
	struct tux_thr *next;
} tux_thr;
tux_thr *tux_cur(void);
tux_thr *tux_thread_new(void);
void tux_thread_attach(tux_thr *t);
void tux_threads_init(void);
void tux_threads_after_fork(void);
void tux_thread_exit(int code) __attribute__((noreturn));
long tux_tgkill(long pid, long tid, long sig);
long tux_futex(uint32_t *uaddr, int op, uint32_t val, const struct timespec *to, uint32_t *uaddr2, uint32_t val3);
long tux_thread_spawn(tux_regs *r, unsigned long fl, uint64_t stack, uint64_t ptid, uint64_t ctid, uint64_t tls);
void tux_tls_new(uint64_t tls);   // neuer Thread: Thread-Zeiger setzen (x86_64: eigener LDT-Eintrag)
void tux_tls_free(void);

// epoll, eventfd, timerfd, signalfd (fdx.c)
long tux_eventfd(unsigned initval, long flags);
long tux_epoll_create(long flags);
long tux_epoll_ctl(int epfd, int op, int fd, const void *ev);
long tux_epoll_wait(int epfd, void *out, int max, long timeout_ms, const uint64_t *sigmask);
long tux_timerfd_create(long clockid, long flags);
long tux_timerfd_settime(int fd, long flags, const int64_t *nv, int64_t *old);
long tux_timerfd_gettime(int fd, int64_t *cur);
long tux_signalfd(int fd, uint64_t mask, long flags);
int tux_fdx_read(int fd, void *buf, size_t n, long *ret);
int tux_fdx_write(int fd, const void *buf, size_t n, long *ret);
void tux_fdx_dup(int oldfd, int newfd);
void tux_fdx_close(int fd);

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
