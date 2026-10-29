// tux – epoll, eventfd, timerfd und signalfd. Darwin kennt nur kqueue; die Linux-Dateideskriptoren werden darauf
// abgebildet:
//   epoll    = ein kqueue, die Anmeldungen (fd, Ereignisse, Daten) stehen in einer Tabelle von tux
//   eventfd  = das Leseende einer Pipe (damit epoll/poll es sehen); Schreiben und Lesen fängt tux ab und rechnet
//              die 8-Byte-Zähler zusammen
//   timerfd  = ein kqueue mit EVFILT_TIMER (selbst per kqueue/poll beobachtbar)
//   signalfd = ein kqueue mit EVFILT_SIGNAL
// Zu jedem solchen Deskriptor gibt es in `tab` ein Objekt; read/write/close/dup in sys.c fragen zuerst hier nach.
#include "tux.h"
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <unistd.h>

enum { K_EVENTFD = 1, K_EPOLL, K_TIMERFD, K_SIGNALFD };
#define FDX_MAX 4096
#define L_O_NONBLOCK 04000
#define L_CLOEXEC 02000000

#define EP_IN 0x001
#define EP_OUT 0x004
#define EP_ERR 0x008
#define EP_HUP 0x010
#define EP_RDHUP 0x2000
#define EP_ONESHOT (1u << 30)
#define EP_ET (1u << 31)

typedef struct { int fd; uint32_t events; uint64_t data; } reg_t;

typedef struct obj {
	int kind, refs;
	int wfd;                       // eventfd: Schreibende der Pipe
	int sem;                       // eventfd: EFD_SEMAPHORE
	reg_t *regs; int nregs, capregs;           // epoll
	int64_t t_interval, t_value, t_start;      // timerfd (ns), t_start: Zeitpunkt des Setzens (monoton)
	int t_rearm;                   // timerfd: nach dem ersten Ablauf mit t_interval weiterlaufen
	int t_clock;
	uint64_t sigmask;              // signalfd
} obj;

static obj *tab[FDX_MAX];
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

static long err(void) { return -tux_errno(errno); }
static int64_t now_ns(int clk)
{
	struct timespec t;
	clock_gettime(clk, &t);
	return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}
static int nonblocking(int fd) { int f = fcntl(fd, F_GETFL); return f >= 0 && (f & O_NONBLOCK); }

static obj *get(int fd, int kind)
{
	if (fd < 0 || fd >= FDX_MAX) return NULL;
	obj *o = tab[fd];
	return o && (!kind || o->kind == kind) ? o : NULL;
}
static int attach(int fd, obj *o)
{
	if (fd >= FDX_MAX) { close(fd); free(o); errno = EMFILE; return -1; }
	pthread_mutex_lock(&lock);
	tab[fd] = o;
	o->refs = 1;
	pthread_mutex_unlock(&lock);
	return fd;
}
static void setcloexec(int fd, long flags) { if (flags & L_CLOEXEC) fcntl(fd, F_SETFD, FD_CLOEXEC); }

// --- Aufrufe aus sys.c für dup/close ---------------------------------------------------------
void tux_fdx_dup(int oldfd, int newfd)
{
	if (newfd < 0 || newfd >= FDX_MAX) return;
	pthread_mutex_lock(&lock);
	if (oldfd >= 0 && oldfd < FDX_MAX && tab[oldfd]) { tab[newfd] = tab[oldfd]; tab[newfd]->refs++; }
	pthread_mutex_unlock(&lock);
}

void tux_fdx_close(int fd)
{
	if (fd < 0 || fd >= FDX_MAX) return;
	pthread_mutex_lock(&lock);
	obj *o = tab[fd];
	tab[fd] = NULL;
	if (o && --o->refs <= 0) {
		if (o->wfd > 0) close(o->wfd);
		free(o->regs);
		free(o);
	}
	// beim Schließen verschwindet der Deskriptor auch aus allen epoll-Mengen
	for (int i = 0; i < FDX_MAX; i++)
		if (tab[i] && tab[i]->kind == K_EPOLL)
			for (int j = 0; j < tab[i]->nregs; j++)
				if (tab[i]->regs[j].fd == fd) { tab[i]->regs[j] = tab[i]->regs[--tab[i]->nregs]; break; }
	pthread_mutex_unlock(&lock);
}

// --- eventfd ---------------------------------------------------------------------------------
long tux_eventfd(unsigned initval, long flags)
{
	int p[2];
	if (pipe(p)) return err();
	// weit weg von den Nummern, die das Programm vergibt (soweit das Limit für offene Dateien es zulässt)
	int w = fcntl(p[1], F_DUPFD_CLOEXEC, 800);
	if (w < 0) w = fcntl(p[1], F_DUPFD_CLOEXEC, 100);
	if (w < 0) w = fcntl(p[1], F_DUPFD_CLOEXEC, 0);
	close(p[1]);
	if (w < 0) { close(p[0]); return err(); }
	fcntl(w, F_SETFL, O_NONBLOCK);
	if (flags & L_O_NONBLOCK) fcntl(p[0], F_SETFL, O_NONBLOCK);
	setcloexec(p[0], flags);
	obj *o = calloc(1, sizeof *o);
	o->kind = K_EVENTFD;
	o->wfd = w;
	o->sem = (flags & 1) != 0;   // EFD_SEMAPHORE
	if (attach(p[0], o) < 0) return err();
	if (initval) {
		uint64_t v = initval;
		if (o->sem) { for (unsigned i = 0; i < initval && i < 4096; i++) { uint64_t one = 1; write(w, &one, 8); } }
		else write(w, &v, 8);
	}
	return p[0];
}

static long eventfd_read(int fd, obj *o, void *buf, size_t n)
{
	if (n < 8) return -EINVAL;
	uint64_t v;
	ssize_t r = read(fd, &v, 8);   // blockiert wie das Original, wenn der Zähler 0 ist
	if (r < 0) return err();
	if (r != 8) return -EIO;
	if (!o->sem) {
		int avail = 0;
		if (ioctl(fd, FIONREAD, &avail) == 0 && avail >= 8) {
			uint64_t rest[512];
			if (avail > (int)sizeof rest) avail = sizeof rest;
			avail &= ~7;
			int fl = fcntl(fd, F_GETFL);
			fcntl(fd, F_SETFL, fl | O_NONBLOCK);
			ssize_t m = read(fd, rest, avail);
			fcntl(fd, F_SETFL, fl);
			for (ssize_t i = 0; i < m / 8; i++) v += rest[i];
		}
	} else v = 1;
	memcpy(buf, &v, 8);
	return 8;
}

static long eventfd_write(obj *o, const void *buf, size_t n)
{
	if (n < 8) return -EINVAL;
	uint64_t v;
	memcpy(&v, buf, 8);
	if (v == ~0ULL) return -EINVAL;
	if (!v) return 8;
	if (o->sem) {
		for (uint64_t i = 0; i < v && i < 4096; i++) { uint64_t one = 1; if (write(o->wfd, &one, 8) < 0) break; }
		return 8;
	}
	if (write(o->wfd, &v, 8) < 0) return errno == EAGAIN ? -EAGAIN : err();
	return 8;
}

// --- epoll -----------------------------------------------------------------------------------
long tux_epoll_create(long flags)
{
	int kq = kqueue();
	if (kq < 0) return err();
	setcloexec(kq, flags);
	obj *o = calloc(1, sizeof *o);
	o->kind = K_EPOLL;
	if (attach(kq, o) < 0) return err();
	return kq;
}

#ifdef TUX_X86_64
typedef struct __attribute__((packed)) { uint32_t events; uint64_t data; } ep_event;   // unter x86_64 gepackt
#else
typedef struct { uint32_t events; uint32_t pad; uint64_t data; } ep_event;
#endif

static reg_t *find_reg(obj *o, int fd)
{
	for (int i = 0; i < o->nregs; i++) if (o->regs[i].fd == fd) return &o->regs[i];
	return NULL;
}

static int ep_apply(int kq, int fd, uint32_t ev, int add)
{
	struct kevent k[2];
	int n = 0;
	unsigned fl = (add ? EV_ADD | EV_ENABLE : EV_DELETE);
	if (add) {
		if (ev & EP_ET) fl |= EV_CLEAR;
		if (ev & EP_ONESHOT) fl |= EV_ONESHOT;
	}
	if (!add || (ev & EP_IN) || !(ev & EP_OUT)) EV_SET(&k[n++], fd, EVFILT_READ, fl, 0, 0, NULL);   // ohne Wunsch: HUP/ERR sehen
	if (!add || (ev & EP_OUT)) EV_SET(&k[n++], fd, EVFILT_WRITE, fl, 0, 0, NULL);
	int rc = 0;
	for (int i = 0; i < n; i++) {
		if (kevent(kq, &k[i], 1, NULL, 0, NULL) < 0 && add) { rc = -1; break; }   // beim Löschen sind Fehler egal
	}
	return rc;
}

long tux_epoll_ctl(int epfd, int op, int fd, const void *evp)
{
	obj *o = get(epfd, K_EPOLL);
	if (!o || fd == epfd) return -EINVAL;
	if (fd < 0) return -EBADF;
	if (fcntl(fd, F_GETFL) < 0) return -EBADF;
	uint32_t ev = 0;
	uint64_t data = 0;
	if (op != 2) {
		if (!evp) return -EFAULT;
		ep_event e;
		memcpy(&e, evp, sizeof e);
		ev = e.events;
		data = e.data;
	}
	long ret = 0;
	pthread_mutex_lock(&lock);
	reg_t *r = find_reg(o, fd);
	if (op == 1) {         // ADD
		if (r) ret = -EEXIST;
		else if (ep_apply(epfd, fd, ev, 1) < 0) ret = (errno == ENOTSUP || errno == EINVAL || errno == EOPNOTSUPP) ? -EPERM : err();
		else {
			if (o->nregs == o->capregs) { o->capregs = o->capregs ? o->capregs * 2 : 16; o->regs = realloc(o->regs, o->capregs * sizeof(reg_t)); }
			o->regs[o->nregs++] = (reg_t){fd, ev, data};
		}
	} else if (op == 2) {  // DEL
		if (!r) ret = -ENOENT;
		else { ep_apply(epfd, fd, 0, 0); *r = o->regs[--o->nregs]; }
	} else if (op == 3) {  // MOD
		if (!r) ret = -ENOENT;
		else {
			ep_apply(epfd, fd, 0, 0);
			if (ep_apply(epfd, fd, ev, 1) < 0) ret = err();
			else { r->events = ev; r->data = data; }
		}
	} else ret = -EINVAL;
	pthread_mutex_unlock(&lock);
	return ret;
}

static int is_sock(int fd) { struct stat st; return fstat(fd, &st) == 0 && S_ISSOCK(st.st_mode); }

long tux_epoll_wait(int epfd, void *out, int max, long timeout_ms, const uint64_t *sigmask)
{
	obj *o = get(epfd, K_EPOLL);
	if (!o) return fcntl(epfd, F_GETFL) < 0 ? -EBADF : -EINVAL;
	if (max <= 0) return -EINVAL;
	struct kevent kev[256];
	int nk = max * 2 > 256 ? 256 : max * 2;
	struct timespec ts, *tp = NULL;
	if (timeout_ms >= 0) { ts.tv_sec = timeout_ms / 1000; ts.tv_nsec = (timeout_ms % 1000) * 1000000L; tp = &ts; }
	sigset_t old;
	int swapped = 0;
	if (sigmask) {
		sigset_t ns;
		tux_guest_to_host_sigset(*sigmask, &ns);
		swapped = pthread_sigmask(SIG_SETMASK, &ns, &old) == 0;
	}
	int n = kevent(epfd, NULL, 0, kev, nk, tp);
	int e = errno;
	if (swapped) pthread_sigmask(SIG_SETMASK, &old, NULL);
	if (n < 0) { errno = e; return err(); }
	ep_event *res = out;
	int cnt = 0;
	pthread_mutex_lock(&lock);
	for (int i = 0; i < n && cnt < max; i++) {
		int fd = (int)kev[i].ident;
		reg_t *r = find_reg(o, fd);
		if (!r) continue;
		uint32_t ev = 0;
		if (kev[i].flags & EV_ERROR) ev |= EP_ERR;
		else if (kev[i].filter == EVFILT_READ) {
			if (kev[i].data > 0 || !(kev[i].flags & EV_EOF)) ev |= EP_IN;
			if (kev[i].flags & EV_EOF) ev |= is_sock(fd) ? (EP_IN | EP_RDHUP) : (EP_IN | EP_HUP);
		} else if (kev[i].filter == EVFILT_WRITE) {
			ev |= EP_OUT;
			if (kev[i].flags & EV_EOF) ev |= EP_ERR | EP_HUP;
		}
		ev &= (r->events | EP_ERR | EP_HUP | EP_RDHUP);
		if (!ev) continue;
		// Lese- und Schreibereignis desselben Deskriptors zu einem zusammenfassen
		int k;
		for (k = 0; k < cnt; k++) if (res[k].data == r->data) break;
		if (k < cnt) { res[k].events |= ev; continue; }
		res[cnt].events = ev;
		res[cnt].data = r->data;
		cnt++;
	}
	pthread_mutex_unlock(&lock);
	// nichts Brauchbares gemeldet (z. B. ein ausgeblendetes Ereignis): bei Zeitlimit 0 ist das ein Ergebnis, sonst
	// würde der Aufrufer irrtümlich mit 0 "Zeit abgelaufen" erhalten
	return cnt;
}

// --- timerfd ---------------------------------------------------------------------------------
long tux_timerfd_create(long clockid, long flags)
{
	int kq = kqueue();
	if (kq < 0) return err();
	setcloexec(kq, flags);
	if (flags & L_O_NONBLOCK) fcntl(kq, F_SETFL, O_NONBLOCK);
	obj *o = calloc(1, sizeof *o);
	o->kind = K_TIMERFD;
	o->t_clock = clockid == 0 || clockid == 11 ? CLOCK_REALTIME : CLOCK_MONOTONIC;
	if (attach(kq, o) < 0) return err();
	return kq;
}

static int timer_arm(int kq, int64_t ns, int oneshot)
{
	struct kevent k;
	if (ns < 1) ns = 1;
	EV_SET(&k, 1, EVFILT_TIMER, EV_ADD | EV_ENABLE | (oneshot ? EV_ONESHOT : 0), NOTE_NSECONDS, ns, NULL);
	return kevent(kq, &k, 1, NULL, 0, NULL);
}

long tux_timerfd_settime(int fd, long flags, const int64_t *nv, int64_t *old)
{
	obj *o = get(fd, K_TIMERFD);
	if (!o) return -EINVAL;
	if (old) {
		int64_t rem = 0;
		if (o->t_value) { rem = o->t_start + o->t_value - now_ns(CLOCK_MONOTONIC); if (rem < 1) rem = o->t_interval ? 1 : 0; }
		old[0] = o->t_interval / 1000000000LL; old[1] = o->t_interval % 1000000000LL;
		old[2] = rem / 1000000000LL; old[3] = rem % 1000000000LL;
	}
	int64_t interval = nv[0] * 1000000000LL + nv[1], value = nv[2] * 1000000000LL + nv[3];
	struct kevent k;
	EV_SET(&k, 1, EVFILT_TIMER, EV_DELETE, 0, 0, NULL);
	kevent(fd, &k, 1, NULL, 0, NULL);
	o->t_interval = interval;
	o->t_rearm = 0;
	if (!value) { o->t_value = 0; return 0; }
	int64_t delay = value;
	if (flags & 1) {   // TFD_TIMER_ABSTIME
		delay = value - now_ns(o->t_clock);
		if (delay < 1) delay = 1;
	}
	o->t_start = now_ns(CLOCK_MONOTONIC);
	o->t_value = delay;
	int rc;
	if (interval && interval == delay) rc = timer_arm(fd, interval, 0);
	else { rc = timer_arm(fd, delay, 1); o->t_rearm = interval != 0; }
	return rc < 0 ? err() : 0;
}

long tux_timerfd_gettime(int fd, int64_t *cur)
{
	obj *o = get(fd, K_TIMERFD);
	if (!o) return -EINVAL;
	int64_t rem = 0;
	if (o->t_value) {
		int64_t el = now_ns(CLOCK_MONOTONIC) - o->t_start;
		if (o->t_interval && el >= o->t_value) rem = o->t_interval - (el - o->t_value) % o->t_interval;
		else rem = o->t_value - el;
		if (rem < 0) rem = 0;
	}
	cur[0] = o->t_interval / 1000000000LL; cur[1] = o->t_interval % 1000000000LL;
	cur[2] = rem / 1000000000LL; cur[3] = rem % 1000000000LL;
	return 0;
}

static long timerfd_read(int fd, obj *o, void *buf, size_t n)
{
	if (n < 8) return -EINVAL;
	struct kevent k;
	struct timespec zero = {0, 0};
	int r = kevent(fd, NULL, 0, &k, 1, nonblocking(fd) ? &zero : NULL);
	if (r < 0) return err();
	if (r == 0) return -EAGAIN;
	uint64_t cnt = k.data > 0 ? (uint64_t)k.data : 1;
	if (o->t_rearm) { o->t_rearm = 0; o->t_start = now_ns(CLOCK_MONOTONIC); o->t_value = o->t_interval; timer_arm(fd, o->t_interval, 0); }
	memcpy(buf, &cnt, 8);
	return 8;
}

// --- signalfd --------------------------------------------------------------------------------
long tux_signalfd(int fd, uint64_t mask, long flags)
{
	obj *o;
	if (fd == -1) {
		int kq = kqueue();
		if (kq < 0) return err();
		setcloexec(kq, flags);
		if (flags & L_O_NONBLOCK) fcntl(kq, F_SETFL, O_NONBLOCK);
		o = calloc(1, sizeof *o);
		o->kind = K_SIGNALFD;
		fd = kq;
		if (attach(kq, o) < 0) return err();
	} else if (!(o = get(fd, K_SIGNALFD))) return -EINVAL;
	for (int s = 1; s < 65; s++) {
		int d = tux_sig_l2d(s);
		if (d < 0) continue;
		int want = (mask >> (s - 1)) & 1, had = (o->sigmask >> (s - 1)) & 1;
		if (want == had) continue;
		struct kevent k;
		EV_SET(&k, d, EVFILT_SIGNAL, want ? EV_ADD | EV_ENABLE : EV_DELETE, 0, 0, NULL);
		kevent(fd, &k, 1, NULL, 0, NULL);
	}
	o->sigmask = mask;
	return fd;
}

static long signalfd_read(int fd, obj *o, void *buf, size_t n)
{
	if (n < 128) return -EINVAL;
	struct kevent kev[32];
	struct timespec zero = {0, 0};
	int max = n / 128 > 32 ? 32 : (int)(n / 128);
	int r = kevent(fd, NULL, 0, kev, max, nonblocking(fd) ? &zero : NULL);
	if (r < 0) return err();
	if (r == 0) return -EAGAIN;
	for (int i = 0; i < r; i++) {
		uint8_t *p = (uint8_t *)buf + i * 128;
		memset(p, 0, 128);
		*(uint32_t *)p = (uint32_t)tux_sig_d2l((int)kev[i].ident);   // ssi_signo
		// das blockierte Signal ist am Prozess noch anhängig: abholen, damit es später nicht noch einmal zugestellt wird
		sigset_t pend, one;
		if (sigpending(&pend) == 0 && sigismember(&pend, (int)kev[i].ident)) {
			sigemptyset(&one);
			sigaddset(&one, (int)kev[i].ident);
			int sig;
			sigwait(&one, &sig);
		}
	}
	return r * 128;
}

// --- read/write-Abfangen ---------------------------------------------------------------------
// Rückgabe 1: der Aufruf war für ein solches Objekt und *ret ist das Ergebnis
int tux_fdx_read(int fd, void *buf, size_t n, long *ret)
{
	obj *o = get(fd, 0);
	if (!o) return 0;
	switch (o->kind) {
	case K_EVENTFD: *ret = eventfd_read(fd, o, buf, n); return 1;
	case K_TIMERFD: *ret = timerfd_read(fd, o, buf, n); return 1;
	case K_SIGNALFD: *ret = signalfd_read(fd, o, buf, n); return 1;
	case K_EPOLL: *ret = -EINVAL; return 1;
	}
	return 0;
}

int tux_fdx_write(int fd, const void *buf, size_t n, long *ret)
{
	obj *o = get(fd, 0);
	if (!o) return 0;
	switch (o->kind) {
	case K_EVENTFD: *ret = eventfd_write(o, buf, n); return 1;
	default: *ret = -EINVAL; return 1;
	}
}
