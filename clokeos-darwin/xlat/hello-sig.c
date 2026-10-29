// Dynamisches x86_64-Testprogramm für Signale: signal, sigaction/SA_SIGINFO, alarm+pause, SIGSEGV/SIGFPE mit siglongjmp,
// SIGCHLD, Sperren mit sigprocmask.
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
static volatile int u1, u2, al, chld, last_si_signo;
static sigjmp_buf jb;
static void h1(int s) { u1++; }
static void h2(int s, siginfo_t *si, void *uc) { u2++; last_si_signo = si->si_signo; }
static void halrm(int s) { al++; }
static void hchld(int s) { chld++; }
static void hjump(int s) { siglongjmp(jb, s); }
static int fails;
#define CHECK(c, m) do { if (!(c)) { printf("FEHLER: %s\n", m); fails++; } } while (0)
int main(void) {
	signal(SIGUSR1, h1); raise(SIGUSR1); CHECK(u1 == 1, "signal+raise");
	struct sigaction sa; memset(&sa, 0, sizeof sa); sa.sa_sigaction = h2; sa.sa_flags = SA_SIGINFO; sigaction(SIGUSR2, &sa, 0);
	kill(getpid(), SIGUSR2); CHECK(u2 == 1 && last_si_signo == SIGUSR2, "SA_SIGINFO+kill");
	sigset_t m; sigemptyset(&m); sigaddset(&m, SIGUSR1); sigprocmask(SIG_BLOCK, &m, 0);
	raise(SIGUSR1); CHECK(u1 == 1, "gesperrt"); sigprocmask(SIG_UNBLOCK, &m, 0); CHECK(u1 == 2, "entsperrt -> zugestellt");
	signal(SIGALRM, halrm); alarm(1); pause(); CHECK(al == 1, "alarm+pause");
	signal(SIGCHLD, hchld); pid_t p = fork(); if (p == 0) _exit(7);
	int st = 0; waitpid(p, &st, 0); CHECK(WIFEXITED(st) && WEXITSTATUS(st) == 7, "wait"); if (!(WIFEXITED(st) && WEXITSTATUS(st) == 7)) printf("p=%d st=%#x\n", p, st); usleep(1000); CHECK(chld >= 1, "SIGCHLD");
	signal(SIGSEGV, hjump); signal(SIGFPE, hjump);
	int r = sigsetjmp(jb, 1); if (r == 0) { volatile int *np = 0; int x = *np; (void)x; CHECK(0, "SEGV nicht ausgelöst"); } else CHECK(r == SIGSEGV, "SIGSEGV gefangen");
	r = sigsetjmp(jb, 1); if (r == 0) { volatile int z = 0; volatile int q = 5 / z; (void)q; CHECK(0, "FPE nicht ausgelöst"); } else CHECK(r == SIGFPE, "SIGFPE gefangen");
	printf("hello-sig: %s (u1=%d u2=%d al=%d chld=%d)\n", fails ? "FEHLER" : "ok", u1, u2, al, chld);
	return fails;
}
