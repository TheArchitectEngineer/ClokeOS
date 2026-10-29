// Signale: Nummern und Strukturen von Linux auf Darwin abbilden, Handler des Programms aufrufen.
#include "tux.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const int l2d[65] = {[0] = 0, 1, 2, 3, 4, 5, 6, 10, 8, 9, 30, 11, 31, 13, 14, 15, -1, 20, 19, 17, 18, 21, 22, 16, 24,
    25, 26, 27, 28, 23, -1, 12};

int tux_sig_l2d(int s) { return s > 0 && s < 32 ? l2d[s] : -1; }

int tux_sig_d2l(int s)
{
	for (int i = 1; i < 32; i++) if (l2d[i] == s) return i;
	return s;
}

// Linux-Maske (Bit n-1 = Signal n) -> Darwin; SIGTRAP und SIGILL sind für tux reserviert
void tux_guest_to_host_sigset(uint64_t l, sigset_t *d)
{
	sigemptyset(d);
	for (int i = 1; i < 32; i++) {
		int ds = l2d[i];
		if (ds > 0 && (l >> (i - 1) & 1) && ds != SIGTRAP && ds != SIGILL) sigaddset(d, ds);
	}
}

static uint64_t host_to_guest_set(const sigset_t *d)
{
	uint64_t l = 0;
	for (int i = 1; i < 32; i++) {
		int ds = l2d[i];
		if (ds > 0 && sigismember(d, ds)) l |= 1ULL << (i - 1);
	}
	return l;
}

long tux_sigprocmask(long how, const uint64_t *set, uint64_t *old, size_t size)
{
	if (size != 8) return -tux_errno(EINVAL);
	sigset_t cur, ns;
	if (pthread_sigmask(SIG_BLOCK, NULL, &cur)) return -tux_errno(EINVAL);
	if (old) *old = host_to_guest_set(&cur);
	if (!set) return 0;
	int h;
	switch (how) {
	case 0: h = SIG_BLOCK; break;
	case 1: h = SIG_UNBLOCK; break;
	case 2: h = SIG_SETMASK; break;
	default: return -tux_errno(EINVAL);
	}
	tux_guest_to_host_sigset(*set, &ns);
	return pthread_sigmask(h, &ns, NULL) ? -tux_errno(EINVAL) : 0;
}

struct gact { uint64_t handler, flags, mask; };
static struct gact acts[65];

#define L_SA_SIGINFO 4
#define L_SA_RESTART 0x10000000
#define L_SA_NODEFER 0x40000000
#define L_SA_RESETHAND 0x80000000UL
#define L_SA_NOCLDSTOP 1

static void tramp(int dsig, siginfo_t *si, void *uc)
{
	int ls = tux_sig_d2l(dsig);
	struct gact g = acts[ls];
	// Linux-siginfo: signo, errno, code, dann je nach Art pid/uid oder Fehleradresse
	struct { int32_t signo, err, code, pad; uint64_t a, b; uint8_t rest[96]; } lsi;
	memset(&lsi, 0, sizeof lsi);
	lsi.signo = ls;
	lsi.code = si ? si->si_code : 0;
	if (si) {
		if (dsig == SIGSEGV || dsig == SIGBUS || dsig == SIGILL || dsig == SIGFPE) lsi.a = (uint64_t)si->si_addr;
		else { lsi.a = (uint32_t)si->si_pid | ((uint64_t)(uint32_t)si->si_uid << 32); }
	}
	static __thread uint8_t luc[4096] __attribute__((aligned(16)));
	memset(luc, 0, sizeof luc);
	if (g.flags & L_SA_RESETHAND) {
		struct sigaction sa;
		memset(&sa, 0, sizeof sa);
		sa.sa_handler = SIG_DFL;
		sigaction(dsig, &sa, NULL);
	}
#ifdef TUX_X86_64
	{
		// Beim Eintritt in den Handler ist FS zurückgesetzt; glibc liest den Stapelschutzwert aus %fs:0x28 (Abstürze bei Adresse 0x28)
		int idx = tux_cur()->ldt_idx;
		if (idx >= 0) {
			unsigned short sel = (unsigned short)((idx << 3) | 7);
			__asm__ volatile("mov %0, %%fs" :: "r"(sel));
		}
	}
#endif
	if (g.handler > 1) {
		if (g.flags & L_SA_SIGINFO) ((void (*)(int, void *, void *))g.handler)(ls, &lsi, luc);
		else ((void (*)(int))g.handler)(ls);
	}
}

static void crash(int sig, siginfo_t *si, void *uc)
{
	ucontext_t *u = uc;
#ifdef TUX_ARM64
	unsigned long long pc = u->uc_mcontext->__ss.__pc, lr = u->uc_mcontext->__ss.__lr, sp = u->uc_mcontext->__ss.__sp;
#else
	unsigned long long pc = u->uc_mcontext->__ss.__rip, lr = 0, sp = u->uc_mcontext->__ss.__rsp;
#endif
	fprintf(stderr, "tux: Programm abgestürzt (Signal %d, Adresse %p, pc=%llx lr=%llx sp=%llx)\n", sig, si->si_addr, pc, lr,
	    sp);
	signal(sig, SIG_DFL);
	raise(sig);
}

void tux_signals_init(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = crash;
	sa.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigaction(SIGSEGV, &sa, NULL);
	sigaction(SIGBUS, &sa, NULL);
}

long tux_sigaction(long sig, const void *actp, void *oldp, size_t size)
{
	if (size != 8 || sig <= 0 || sig > 64) return -tux_errno(EINVAL);
	int ds = tux_sig_l2d((int)sig);
	if (ds < 0) return actp ? (sig == 16 || sig == 30 ? 0 : -tux_errno(EINVAL)) : 0;
	if (sig == 9 || sig == 19) return actp ? -tux_errno(EINVAL) : 0;
	// Kernel-Aufbau: arm64 handler, flags, mask; x86_64 handler, flags, restorer, mask
	const uint64_t *a = actp;
	uint64_t *o = oldp;
	if (o) {
		o[0] = acts[sig].handler;
		o[1] = acts[sig].flags;
#ifdef TUX_ARM64
		o[2] = acts[sig].mask;
#else
		o[2] = 0;
		o[3] = acts[sig].mask;
#endif
	}
	if (!a) return 0;
	struct gact n;
	n.handler = a[0];
	n.flags = a[1];
#ifdef TUX_ARM64
	n.mask = a[2];
#else
	n.mask = a[3];
#endif
	acts[sig] = n;
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	if (n.handler == 0) sa.sa_handler = SIG_DFL;
	else if (n.handler == 1) sa.sa_handler = SIG_IGN;
	else {
		sa.sa_sigaction = tramp;
		sa.sa_flags = SA_SIGINFO;
		if (n.flags & L_SA_RESTART) sa.sa_flags |= SA_RESTART;
		if (n.flags & L_SA_NODEFER) sa.sa_flags |= SA_NODEFER;
		tux_guest_to_host_sigset(n.mask, &sa.sa_mask);
	}
	if (n.flags & L_SA_NOCLDSTOP) sa.sa_flags |= SA_NOCLDSTOP;
	// SIGSEGV/SIGBUS behalten ihre eigene Behandlung, wenn das Programm nur SIG_DFL will
	if (ds == SIGSEGV || ds == SIGBUS) { if (n.handler == 0) { tux_signals_init(); return 0; } }
	if (sigaction(ds, &sa, NULL)) return -tux_errno(EINVAL);
	return 0;
}
