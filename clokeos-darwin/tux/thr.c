// tux – Threads des Linux-Programms: jeder Linux-Thread ist ein Darwin-pthread. Hier liegen die Thread-Tabelle,
// futex (Wartelisten mit Bedingungsvariablen), das Beenden eines Threads und tgkill.
#include "tux.h"
#include <errno.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static pthread_mutex_t tl = PTHREAD_MUTEX_INITIALIZER;   // schützt die Thread-Liste
static tux_thr *threads;
static int nthreads, next_tid;
static __thread tux_thr *self;

tux_thr *tux_cur(void) { return self; }

static void add_thread(tux_thr *t)
{
	pthread_mutex_lock(&tl);
	t->next = threads;
	threads = t;
	nthreads++;
	pthread_mutex_unlock(&tl);
}

void tux_threads_init(void)
{
	self = calloc(1, sizeof *self);
	self->tid = getpid();
	self->ldt_idx = -1;
	self->pt = pthread_self();
	next_tid = self->tid + 1;
	add_thread(self);
}

// Im Kind nach fork(): nur der aufrufende Thread lebt weiter
void tux_threads_after_fork(void)
{
	pthread_mutex_init(&tl, NULL);
	self->tid = getpid();
	self->next = NULL;
	threads = self;
	nthreads = 1;
	next_tid = self->tid + 1;
}

tux_thr *tux_thread_new(void)
{
	tux_thr *t = calloc(1, sizeof *t);
	pthread_mutex_lock(&tl);
	t->tid = next_tid++;
	t->ldt_idx = -1;
	pthread_mutex_unlock(&tl);
	return t;
}

// Erste Handlung im neuen pthread
void tux_thread_attach(tux_thr *t)
{
	t->pt = pthread_self();
	self = t;
	add_thread(t);
}

static void thread_finish(void *arg)
{
	tux_thr *t = arg;
	if (t->clear_tid) {
		__atomic_store_n((uint32_t *)t->clear_tid, 0, __ATOMIC_SEQ_CST);
		tux_futex((uint32_t *)t->clear_tid, 1, INT_MAX, NULL, NULL, 0);
	}
	if (t->defer_len) munmap((void *)t->defer_addr, t->defer_len);
	tux_tls_free();
	free(t);
	pthread_exit(NULL);
}

extern void tux_on_stack(void (*fn)(void *), void *arg, void *sp) __attribute__((noreturn));

// Linux-`exit`: beendet nur diesen Thread; der letzte beendet den Prozess. Der Rest läuft auf dem Stapel des
// pthreads, weil der Stapel des Gastes nach dem Löschen von clear_tid schon von pthread_join freigegeben sein kann.
void tux_thread_exit(int code)
{
	tux_thr *t = self;
	pthread_mutex_lock(&tl);
	for (tux_thr **p = &threads; *p; p = &(*p)->next)
		if (*p == t) { *p = t->next; break; }
	int left = --nthreads;
	pthread_mutex_unlock(&tl);
	if (left <= 0) _exit(code);
	if (t->host_sp) tux_on_stack(thread_finish, t, t->host_sp);
	thread_finish(t);
	__builtin_unreachable();
}

long tux_tgkill(long pid, long tid, long sig)
{
	int s = sig ? tux_sig_l2d((int)sig) : 0;
	if (sig && s < 0) return -tux_errno(EINVAL);
	if (pid > 0 && pid != getpid()) return kill((pid_t)pid, s) ? -tux_errno(errno) : 0;
	pthread_t pt = 0;
	pthread_mutex_lock(&tl);
	for (tux_thr *t = threads; t; t = t->next)
		if (t->tid == tid) { pt = t->pt; break; }
	pthread_mutex_unlock(&tl);
	if (!pt) return -tux_errno(ESRCH);
	if (!s) return 0;
	int e = pthread_kill(pt, s);
	return e ? -tux_errno(e) : 0;
}

// ---------------------------------------------------------------------------------------------
// futex
typedef struct waiter {
	uint32_t *addr;
	uint32_t mask;
	int woken;
	pthread_cond_t cv;
	struct waiter *next;
} waiter;
static pthread_mutex_t fm = PTHREAD_MUTEX_INITIALIZER;
static waiter *wq, *wq_tail;

static void wq_remove(waiter *w)
{
	waiter *prev = NULL;
	for (waiter *x = wq; x; prev = x, x = x->next)
		if (x == w) {
			if (prev) prev->next = x->next; else wq = x->next;
			if (wq_tail == w) wq_tail = prev;
			return;
		}
}

static int wake_locked(uint32_t *addr, int n, uint32_t mask)
{
	int c = 0;
	for (waiter *w = wq; w && c < n; w = w->next)
		if (w->addr == addr && !w->woken && (w->mask & mask)) {
			w->woken = 1;
			pthread_cond_signal(&w->cv);
			c++;
		}
	return c;
}

static long futex_wait(uint32_t *addr, uint32_t val, const struct timespec *to, int abs, int clk, uint32_t mask)
{
	struct timespec rel;
	if (to) {
		rel = *to;
		if (abs) {
			struct timespec now;
			clock_gettime(clk, &now);
			rel.tv_sec -= now.tv_sec;
			rel.tv_nsec -= now.tv_nsec;
			if (rel.tv_nsec < 0) { rel.tv_nsec += 1000000000L; rel.tv_sec--; }
			if (rel.tv_sec < 0) { rel.tv_sec = 0; rel.tv_nsec = 0; }
		}
	}
	waiter w = {addr, mask, 0, PTHREAD_COND_INITIALIZER, NULL};
	pthread_mutex_lock(&fm);
	if (__atomic_load_n(addr, __ATOMIC_SEQ_CST) != val) {
		pthread_mutex_unlock(&fm);
		return -tux_errno(EAGAIN);
	}
	if (wq_tail) wq_tail->next = &w; else wq = &w;
	wq_tail = &w;
	long ret = 0;
	while (!w.woken) {
		int e = to ? pthread_cond_timedwait_relative_np(&w.cv, &fm, &rel) : pthread_cond_wait(&w.cv, &fm);
		if (e == ETIMEDOUT && !w.woken) { ret = -tux_errno(ETIMEDOUT); break; }
	}
	wq_remove(&w);
	pthread_mutex_unlock(&fm);
	pthread_cond_destroy(&w.cv);
	return ret;
}

long tux_futex(uint32_t *uaddr, int op, uint32_t val, const struct timespec *to, uint32_t *uaddr2, uint32_t val3)
{
	int cmd = op & 0x7f;
	switch (cmd) {
	case 0: return futex_wait(uaddr, val, to, 0, CLOCK_MONOTONIC, ~0u);                          // WAIT
	case 9: return futex_wait(uaddr, val, to, 1, (op & 256) ? CLOCK_REALTIME : CLOCK_MONOTONIC, val3);   // WAIT_BITSET
	case 1: case 10: {                                                                           // WAKE, WAKE_BITSET
		pthread_mutex_lock(&fm);
		int c = wake_locked(uaddr, (int)val, cmd == 10 ? val3 : ~0u);
		pthread_mutex_unlock(&fm);
		return c;
	}
	case 3: case 4: {                                                                            // REQUEUE, CMP_REQUEUE
		pthread_mutex_lock(&fm);
		if (cmd == 4 && __atomic_load_n(uaddr, __ATOMIC_SEQ_CST) != val3) {
			pthread_mutex_unlock(&fm);
			return -tux_errno(EAGAIN);
		}
		int c = wake_locked(uaddr, (int)val, ~0u);
		long max = (long)(uintptr_t)to;   // bei REQUEUE steht hier die Höchstzahl
		for (waiter *w = wq; w && max > 0; w = w->next)
			if (w->addr == uaddr && !w->woken) { w->addr = uaddr2; max--; c++; }
		pthread_mutex_unlock(&fm);
		return c;
	}
	case 5: {                                                                                    // WAKE_OP
		int opc = (val3 >> 28) & 0xf, cmp = (val3 >> 24) & 0xf;
		int oparg = ((int)(val3 << 8) >> 20), cmparg = ((int)(val3 << 20) >> 20);   // je 12 Bit mit Vorzeichen
		if (opc & 8) { opc &= 7; oparg = 1 << (oparg & 31); }
		uint32_t old, nw;
		do {
			old = __atomic_load_n(uaddr2, __ATOMIC_SEQ_CST);
			switch (opc) {
			case 0: nw = oparg; break;
			case 1: nw = old + oparg; break;
			case 2: nw = old | oparg; break;
			case 3: nw = old & ~oparg; break;
			case 4: nw = old ^ oparg; break;
			default: return -tux_errno(ENOSYS);
			}
		} while (!__atomic_compare_exchange_n(uaddr2, &old, nw, 0, __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST));
		pthread_mutex_lock(&fm);
		int c = wake_locked(uaddr, (int)val, ~0u);
		int o = (int)old, hit;
		switch (cmp) {
		case 0: hit = o == cmparg; break;
		case 1: hit = o != cmparg; break;
		case 2: hit = o < cmparg; break;
		case 3: hit = o <= cmparg; break;
		case 4: hit = o > cmparg; break;
		case 5: hit = o >= cmparg; break;
		default: hit = 0;
		}
		if (hit) c += wake_locked(uaddr2, (int)(uintptr_t)to, ~0u);
		pthread_mutex_unlock(&fm);
		return c;
	}
	default: return -tux_errno(ENOSYS);
	}
}
