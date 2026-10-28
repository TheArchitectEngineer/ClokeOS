// Übersetzung der Linux-Systemaufrufe in Darwin-Aufrufe und Pfade in den Linux-Baum (/TUX).
#include "tux.h"
#include "nr.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/param.h>
#include <sys/random.h>
#include <sys/uio.h>
#include <sys/resource.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <sys/times.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define AT_FDCWD_L (-100)

// ---------------------------------------------------------------------------------------------
// Fehlernummern
int tux_errno(int e)
{
	switch (e) {
	case EDEADLK: return 35;
	case EAGAIN: return 11;
	case EINPROGRESS: return 115;
	case EALREADY: return 114;
	case ENOTSOCK: return 88;
	case EDESTADDRREQ: return 89;
	case EMSGSIZE: return 90;
	case EPROTOTYPE: return 91;
	case ENOPROTOOPT: return 92;
	case EPROTONOSUPPORT: return 93;
	case ESOCKTNOSUPPORT: return 94;
	case ENOTSUP: return 95;
	case EPFNOSUPPORT: return 96;
	case EAFNOSUPPORT: return 97;
	case EADDRINUSE: return 98;
	case EADDRNOTAVAIL: return 99;
	case ENETDOWN: return 100;
	case ENETUNREACH: return 101;
	case ENETRESET: return 102;
	case ECONNABORTED: return 103;
	case ECONNRESET: return 104;
	case ENOBUFS: return 105;
	case EISCONN: return 106;
	case ENOTCONN: return 107;
	case ESHUTDOWN: return 108;
	case ETIMEDOUT: return 110;
	case ECONNREFUSED: return 111;
	case ELOOP: return 40;
	case ENAMETOOLONG: return 36;
	case EHOSTDOWN: return 112;
	case EHOSTUNREACH: return 113;
	case ENOTEMPTY: return 39;
	case EUSERS: return 87;
	case EDQUOT: return 122;
	case ESTALE: return 116;
	case ENOLCK: return 37;
	case ENOSYS: return 38;
	case EFTYPE: return 22;
	case EOVERFLOW: return 75;
	case ECANCELED: return 125;
	case EIDRM: return 43;
	case ENOMSG: return 42;
	case EILSEQ: return 84;
	case ENOATTR: return 61;
	case EBADMSG: return 74;
	case EMULTIHOP: return 72;
	case ENODATA: return 61;
	case ENOLINK: return 67;
	case ENOSR: return 63;
	case ENOSTR: return 60;
	case EPROTO: return 71;
	case ETIME: return 62;
	case EOWNERDEAD: return 130;
	case ENOTRECOVERABLE: return 131;
	default: return e;   // 1 bis 34 (außer 11 und 35) sind gleich
	}
}

const char *tux_errstr(int e)
{
	switch (e) {
	case 2: return "No such file or directory";
	case 8: return "Exec format error";
	case 13: return "Permission denied";
	case 20: return "Not a directory";
	case 21: return "Is a directory";
	case 40: return "Too many levels of symbolic links";
	default: { static char b[32]; snprintf(b, sizeof b, "Fehler %d", e); return b; }
	}
}

#define ERR() (-(long)tux_errno(errno))
#define E(x) ({ long _r = (long)(x); _r == -1 ? ERR() : _r; })

// ---------------------------------------------------------------------------------------------
// Pfade: /TUX ist die Wurzel; /dev, /tmp, /Users und /Volumes sind die des Macs
static const char *const through[] = {"/dev", "/tmp", "/Users", "/Volumes", "/private/tmp", NULL};

static int is_through(const char *g)
{
	for (int i = 0; through[i]; i++)
		if (!strcmp(g, through[i])) return 1;
	return 0;
}

static void guest_to_host(const char *g, char *host)
{
	for (int i = 0; through[i]; i++) {
		size_t n = strlen(through[i]);
		if (!strncmp(g, through[i], n) && (g[n] == 0 || g[n] == '/')) {
			strlcpy(host, g, PATH_MAX);
			return;
		}
	}
	if (!g[0] || !strcmp(g, "/")) strlcpy(host, tux_root, PATH_MAX);
	else snprintf(host, PATH_MAX, "%s%s", !strcmp(tux_root, "/") ? "" : tux_root, g);
}

static void host_to_guest(const char *h, char *g)
{
	size_t n = strlen(tux_root);
	if (!strncmp(h, tux_root, n) && (h[n] == '/' || h[n] == 0)) strlcpy(g, h[n] ? h + n : "/", PATH_MAX);
	else strlcpy(g, h, PATH_MAX);
}

static void pop_comp(char *cur)
{
	char *s = strrchr(cur, '/');
	if (s) *s = 0;
}

int tux_resolve(int dirfd, const char *path, int follow, char *host, char *guest)
{
	static __thread char w1[2 * PATH_MAX], w2[2 * PATH_MAX];
	char cur[PATH_MAX];
	if (!path[0]) return -ENOENT;
	size_t plen = strlen(path);
	int want_dir = path[plen - 1] == '/';
	cur[0] = 0;
	if (path[0] != '/') {
		char base[PATH_MAX];
		if (dirfd == AT_FDCWD_L) strlcpy(base, tux_cwd, sizeof base);
		else {
			char hp[PATH_MAX];
			if (fcntl(dirfd, F_GETPATH, hp) < 0) return -EBADF;
			host_to_guest(hp, base);
		}
		if (strcmp(base, "/")) strlcpy(cur, base, sizeof cur);
	}
	if (plen >= sizeof w1) return -ENAMETOOLONG;
	char *work = w1, *other = w2;
	strlcpy(work, path, sizeof w1);
	char *p = work;
	int links = 0;
	for (;;) {
		while (*p == '/') p++;
		if (!*p) break;
		char *comp = p;
		while (*p && *p != '/') p++;
		size_t cl = p - comp;
		char save = *p;
		*p = 0;
		if (!strcmp(comp, ".")) { *p = save; continue; }
		if (!strcmp(comp, "..")) { pop_comp(cur); *p = save; continue; }
		if (strlen(cur) + cl + 2 >= sizeof cur) return -ENAMETOOLONG;
		strlcat(cur, "/", sizeof cur);
		strlcat(cur, comp, sizeof cur);
		*p = save;
		if (is_through(cur)) {
			// ab hier gilt der Mac: den Rest nur noch bereinigen, Verknüpfungen löst Darwin auf
			char rest[PATH_MAX];
			strlcpy(rest, p, sizeof rest);
			char *q = rest;
			while (*q) {
				while (*q == '/') q++;
				if (!*q) break;
				char *c = q;
				while (*q && *q != '/') q++;
				char sv = *q;
				*q = 0;
				if (!strcmp(c, "..")) pop_comp(cur);
				else if (strcmp(c, ".")) { strlcat(cur, "/", sizeof cur); strlcat(cur, c, sizeof cur); }
				*q = sv;
			}
			goto done;
		}
		int last = 1;
		for (char *t = p; *t; t++) if (*t != '/') { last = 0; break; }
		if (!last || follow) {
			char hp[PATH_MAX], tgt[PATH_MAX];
			guest_to_host(cur, hp);
			ssize_t n = readlink(hp, tgt, sizeof tgt - 1);
			if (n >= 0) {
				if (++links > 40) return -ELOOP;
				tgt[n] = 0;
				pop_comp(cur);
				if (tgt[0] == '/') cur[0] = 0;
				// neuer Rest = Ziel + noch nicht gelesener Teil
				if (strlen(tgt) + strlen(p) + 2 >= sizeof w1) return -ENAMETOOLONG;
				snprintf(other, sizeof w1, "%s%s%s", tgt, "/", p);
				char *tmp = work; work = other; other = tmp;
				p = work;
			}
		}
	}
done:
	if (!cur[0]) strlcpy(cur, "/", sizeof cur);
	if (guest) strlcpy(guest, cur, PATH_MAX);
	guest_to_host(cur, host);
	if (want_dir && strcmp(cur, "/")) strlcat(host, "/", PATH_MAX);
	return 0;
}

#define PATH_(dirfd, p, follow) \
	char host[PATH_MAX]; \
	{ long _e = tux_resolve((int)(dirfd), (const char *)(p), (follow), host, NULL); if (_e < 0) return _e; }

// ---------------------------------------------------------------------------------------------
// Umrechnung von Strukturen und Flags
#ifdef TUX_ARM64
#define L_O_DIRECTORY 040000
#define L_O_NOFOLLOW 0100000
#else
#define L_O_DIRECTORY 0200000
#define L_O_NOFOLLOW 0400000
#endif

static int open_flags(long f)
{
	int d = (int)(f & 3);
	if (f & 0100) d |= O_CREAT;
	if (f & 0200) d |= O_EXCL;
	if (f & 0400) d |= O_NOCTTY;
	if (f & 01000) d |= O_TRUNC;
	if (f & 02000) d |= O_APPEND;
	if (f & 04000) d |= O_NONBLOCK;
	if (f & 04010000) d |= O_SYNC;
	if (f & L_O_DIRECTORY) d |= O_DIRECTORY;
	if (f & L_O_NOFOLLOW) d |= O_NOFOLLOW;
	if (f & 02000000) d |= O_CLOEXEC;
	return d;
}

struct lstat_t {   // struct stat von Linux
#ifdef TUX_ARM64
	uint64_t st_dev, st_ino;
	uint32_t st_mode, st_nlink, st_uid, st_gid;
	uint64_t st_rdev, pad1;
	int64_t st_size;
	int32_t st_blksize, pad2;
	int64_t st_blocks;
	int64_t st_atime_s, st_atime_ns, st_mtime_s, st_mtime_ns, st_ctime_s, st_ctime_ns;
	uint32_t unused[2];
#else
	uint64_t st_dev, st_ino, st_nlink;
	uint32_t st_mode, st_uid, st_gid, pad0;
	uint64_t st_rdev;
	int64_t st_size, st_blksize, st_blocks;
	int64_t st_atime_s, st_atime_ns, st_mtime_s, st_mtime_ns, st_ctime_s, st_ctime_ns;
	int64_t unused[3];
#endif
};

static void conv_stat(const struct stat *s, struct lstat_t *l)
{
	memset(l, 0, sizeof *l);
	l->st_dev = s->st_dev;
	l->st_ino = s->st_ino;
	l->st_mode = s->st_mode;
	l->st_nlink = s->st_nlink;
	l->st_uid = s->st_uid;
	l->st_gid = s->st_gid;
	l->st_rdev = s->st_rdev;
	l->st_size = s->st_size;
	l->st_blksize = s->st_blksize;
	l->st_blocks = s->st_blocks;
	l->st_atime_s = s->st_atimespec.tv_sec;
	l->st_atime_ns = s->st_atimespec.tv_nsec;
	l->st_mtime_s = s->st_mtimespec.tv_sec;
	l->st_mtime_ns = s->st_mtimespec.tv_nsec;
	l->st_ctime_s = s->st_ctimespec.tv_sec;
	l->st_ctime_ns = s->st_ctimespec.tv_nsec;
}

struct lstatx_t {  // struct statx
	uint32_t mask, blksize;
	uint64_t attributes;
	uint32_t nlink, uid, gid;
	uint16_t mode, pad;
	uint64_t ino, size, blocks, attributes_mask;
	struct { int64_t sec; uint32_t nsec; int32_t pad; } atime, btime, ctime, mtime;
	uint32_t rdev_major, rdev_minor, dev_major, dev_minor;
	uint64_t spare[14];
};

static void conv_statx(const struct stat *s, struct lstatx_t *x)
{
	memset(x, 0, sizeof *x);
	x->mask = 0x7ff;   // STATX_BASIC_STATS
	x->blksize = s->st_blksize;
	x->nlink = s->st_nlink;
	x->uid = s->st_uid;
	x->gid = s->st_gid;
	x->mode = s->st_mode;
	x->ino = s->st_ino;
	x->size = s->st_size;
	x->blocks = s->st_blocks;
	x->atime.sec = s->st_atimespec.tv_sec; x->atime.nsec = s->st_atimespec.tv_nsec;
	x->mtime.sec = s->st_mtimespec.tv_sec; x->mtime.nsec = s->st_mtimespec.tv_nsec;
	x->ctime.sec = s->st_ctimespec.tv_sec; x->ctime.nsec = s->st_ctimespec.tv_nsec;
	x->btime.sec = s->st_birthtimespec.tv_sec; x->btime.nsec = s->st_birthtimespec.tv_nsec;
	x->rdev_major = major(s->st_rdev); x->rdev_minor = minor(s->st_rdev);
	x->dev_major = major(s->st_dev); x->dev_minor = minor(s->st_dev);
}

static int clock_l2d(long c)
{
	switch (c) {
	case 0: return CLOCK_REALTIME;
	case 1: case 6: case 7: return CLOCK_MONOTONIC;   // auch COARSE und BOOTTIME
	case 4: return CLOCK_MONOTONIC_RAW;
	case 2: return CLOCK_PROCESS_CPUTIME_ID;
	case 3: return CLOCK_THREAD_CPUTIME_ID;
	case 5: return CLOCK_REALTIME;
	default: return -1;
	}
}

// ---------------------------------------------------------------------------------------------
// Terminal
struct ltermios { uint32_t iflag, oflag, cflag, lflag; uint8_t line; uint8_t cc[19]; };
struct bitmap { uint32_t l, d; };
static const struct bitmap imap[] = {{1, IGNBRK}, {2, BRKINT}, {4, IGNPAR}, {8, PARMRK}, {0x10, INPCK}, {0x20, ISTRIP},
    {0x40, INLCR}, {0x80, IGNCR}, {0x100, ICRNL}, {0x400, IXON}, {0x800, IXANY}, {0x1000, IXOFF}, {0x2000, IMAXBEL},
    {0x4000, IUTF8}, {0, 0}};
static const struct bitmap omap[] = {{1, OPOST}, {4, ONLCR}, {8, OCRNL}, {0x10, ONOCR}, {0x20, ONLRET}, {0, 0}};
static const struct bitmap lmap[] = {{1, ISIG}, {2, ICANON}, {8, ECHO}, {0x10, ECHOE}, {0x20, ECHOK}, {0x40, ECHONL},
    {0x80, NOFLSH}, {0x100, TOSTOP}, {0x200, ECHOCTL}, {0x400, ECHOPRT}, {0x800, ECHOKE}, {0x1000, FLUSHO},
    {0x4000, PENDIN}, {0x8000, IEXTEN}, {0, 0}};
static const struct bitmap cmap[] = {{0x40, CSTOPB}, {0x80, CREAD}, {0x100, PARENB}, {0x200, PARODD}, {0x400, HUPCL},
    {0x800, CLOCAL}, {0, 0}};
// Linux-Index im c_cc -> Darwin-Index
static const struct { int l, d; } ccmap[] = {{0, VINTR}, {1, VQUIT}, {2, VERASE}, {3, VKILL}, {4, VEOF}, {5, VTIME},
    {6, VMIN}, {8, VSTART}, {9, VSTOP}, {10, VSUSP}, {11, VEOL}, {12, VREPRINT}, {13, VDISCARD}, {14, VWERASE},
    {15, VLNEXT}, {16, VEOL2}, {-1, 0}};
static const struct { uint32_t l; speed_t d; } speeds[] = {{0, B0}, {1, B50}, {2, B75}, {3, B110}, {4, B134}, {5, B150},
    {6, B200}, {7, B300}, {8, B600}, {9, B1200}, {10, B1800}, {11, B2400}, {12, B4800}, {13, B9600}, {14, B19200},
    {15, B38400}, {0x1001, B57600}, {0x1002, B115200}, {0x1003, B230400}, {0, 0xffffffff}};

static uint32_t bits_d2l(const struct bitmap *m, tcflag_t d)
{
	uint32_t l = 0;
	for (; m->l; m++) if (d & m->d) l |= m->l;
	return l;
}
static tcflag_t bits_l2d(const struct bitmap *m, uint32_t l)
{
	tcflag_t d = 0;
	for (; m->l; m++) if (l & m->l) d |= m->d;
	return d;
}

static void termios_d2l(const struct termios *t, struct ltermios *l)
{
	memset(l, 0, sizeof *l);
	l->iflag = bits_d2l(imap, t->c_iflag);
	l->oflag = bits_d2l(omap, t->c_oflag);
	l->lflag = bits_d2l(lmap, t->c_lflag);
	l->cflag = bits_d2l(cmap, t->c_cflag);
	switch (t->c_cflag & CSIZE) {
	case CS5: break;
	case CS6: l->cflag |= 0x10; break;
	case CS7: l->cflag |= 0x20; break;
	default: l->cflag |= 0x30;
	}
	for (int i = 0; speeds[i].d != 0xffffffff; i++)
		if (speeds[i].d == t->c_ospeed) l->cflag |= speeds[i].l;
	for (int i = 0; ccmap[i].l >= 0; i++) l->cc[ccmap[i].l] = t->c_cc[ccmap[i].d];
}

static void termios_l2d(const struct ltermios *l, struct termios *t)
{
	t->c_iflag = bits_l2d(imap, l->iflag);
	t->c_oflag = bits_l2d(omap, l->oflag);
	t->c_lflag = bits_l2d(lmap, l->lflag);
	t->c_cflag = bits_l2d(cmap, l->cflag);
	switch (l->cflag & 0x30) {
	case 0: t->c_cflag |= CS5; break;
	case 0x10: t->c_cflag |= CS6; break;
	case 0x20: t->c_cflag |= CS7; break;
	default: t->c_cflag |= CS8;
	}
	speed_t sp = B38400;
	for (int i = 0; speeds[i].d != 0xffffffff; i++)
		if (speeds[i].l == (l->cflag & 0x100f)) sp = speeds[i].d;
	t->c_ispeed = t->c_ospeed = sp;
	for (int i = 0; ccmap[i].l >= 0; i++) t->c_cc[ccmap[i].d] = l->cc[ccmap[i].l];
}

static long do_ioctl(int fd, unsigned long req, void *arg)
{
	switch (req) {
	case 0x5401: case 0x5402: case 0x5403: case 0x5404: {   // TCGETS, TCSETS, TCSETSW, TCSETSF
		struct termios t;
		if (req == 0x5401) {
			if (tcgetattr(fd, &t) < 0) return ERR();
			termios_d2l(&t, arg);
			return 0;
		}
		if (tcgetattr(fd, &t) < 0) return ERR();
		termios_l2d(arg, &t);
		return E(tcsetattr(fd, req == 0x5402 ? TCSANOW : req == 0x5403 ? TCSADRAIN : TCSAFLUSH, &t));
	}
	case 0x5413: return E(ioctl(fd, TIOCGWINSZ, arg));
	case 0x5414: return E(ioctl(fd, TIOCSWINSZ, arg));
	case 0x540F: { pid_t p = tcgetpgrp(fd); if (p < 0) return ERR(); *(int *)arg = p; return 0; }
	case 0x5410: return E(tcsetpgrp(fd, *(int *)arg));
	case 0x540E: return E(ioctl(fd, TIOCSCTTY, 0));
	case 0x5422: return E(ioctl(fd, TIOCNOTTY, 0));
	case 0x5429: { return -ENOTTY;   /* tcgetsid fehlt in PureDarwins libSystem */ }
	case 0x541B: return E(ioctl(fd, FIONREAD, arg));
	case 0x5421: return E(ioctl(fd, FIONBIO, arg));
	case 0x5450: return E(fcntl(fd, F_SETFD, 0));            // FIONCLEX
	case 0x5451: return E(fcntl(fd, F_SETFD, FD_CLOEXEC));   // FIOCLEX
	case 0x540B: {   // TCFLSH
		int q = (long)arg == 0 ? TCIFLUSH : (long)arg == 1 ? TCOFLUSH : TCIOFLUSH;
		return E(tcflush(fd, q));
	}
	case 0x5409: return E(tcsendbreak(fd, 0));
	default:
		if (tux_trace) fprintf(stderr, "tux: ioctl 0x%lx nicht übersetzt\n", req);
		return -ENOTTY;
	}
}

// ---------------------------------------------------------------------------------------------
// brk
static uint8_t *brk_base, *brk_cur, *brk_end;

void tux_sys_init(void)
{
	size_t sz = 256UL << 20;
	brk_base = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_NORESERVE, -1, 0);
	if (brk_base == MAP_FAILED) brk_base = NULL;
	brk_cur = brk_base;
	brk_end = brk_base + sz;
	if (!getenv("HOME")) setenv("HOME", "/", 0);
}

// ---------------------------------------------------------------------------------------------
// Prozesse
static long do_execve(const char *path, char **argv, char **envp)
{
	char host[PATH_MAX], canon[PATH_MAX];
	long e = tux_resolve(AT_FDCWD_L, path, 1, host, canon);
	if (e < 0) return e;
	struct stat st;
	if (stat(host, &st) != 0) return ERR();
	if (S_ISDIR(st.st_mode)) return -EACCES;
	if (access(host, X_OK) != 0) return -EACCES;
	// tux mit denselben Einstellungen noch einmal starten; der Gast sieht argv unverändert
	char *nargv[1040];
	int k = 0;
	nargv[k++] = tux_self;
	nargv[k++] = "-r";
	nargv[k++] = tux_root;
	nargv[k++] = "-C";
	nargv[k++] = tux_cwd;
	nargv[k++] = "-0";
	nargv[k++] = argv && argv[0] ? argv[0] : (char *)path;
	nargv[k++] = "--";
	nargv[k++] = canon;
	if (argv) for (int i = 1; argv[i] && k < 1038; i++) nargv[k++] = argv[i];
	nargv[k] = NULL;
	char tr[8];
	(void)tr;
	execve(tux_self, nargv, envp);
	return ERR();
}

static int sig_status_d2l(int st)
{
	if (WIFSIGNALED(st)) return (st & ~0x7f) | tux_sig_d2l(WTERMSIG(st));
	if (WIFSTOPPED(st)) return (tux_sig_d2l(WSTOPSIG(st)) << 8) | 0x7f;
	return st;
}

// ---------------------------------------------------------------------------------------------
// Verzeichnisse
struct ldirent { uint64_t ino; int64_t off; uint16_t reclen; uint8_t type; char name[]; };

static long do_getdents64(int fd, void *buf, size_t cnt)
{
	static __thread char dbuf[16384];
	// Darwin (getdirentries64, Nummer 344) liefert Einträge mit 64-Bit-Inode. Ein Linux-Eintrag ist höchstens
	// ein Viertel größer als der von Darwin, also nur so viel anfordern, dass alles in den Puffer passt.
	if (cnt < 600) return -EINVAL;
	size_t ask = cnt * 3 / 4 - 16;
	if (ask > sizeof dbuf) ask = sizeof dbuf;
	long basep = 0;
	int n = (int)syscall(344, fd, dbuf, ask, &basep);
	if (n < 0) return ERR();
	uint8_t *out = buf;
	size_t used = 0, off = 0;
	while (off < (size_t)n) {
		struct dirent *d = (struct dirent *)(dbuf + off);
		size_t nl = d->d_namlen;
		size_t rl = (sizeof(struct ldirent) + nl + 1 + 7) & ~7UL;
		off += d->d_reclen;
		if (d->d_ino == 0) continue;
		struct ldirent *l = (struct ldirent *)(out + used);
		l->ino = d->d_ino;
		l->off = d->d_seekoff;
		l->reclen = rl;
		l->type = d->d_type;
		memcpy(l->name, d->d_name, nl);
		memset(l->name + nl, 0, rl - sizeof(struct ldirent) - nl);
		used += rl;
	}
	return (long)used;
}

// ---------------------------------------------------------------------------------------------
// Hauptschalter
static long sys_mmap(uint64_t addr, uint64_t len, int prot, int flags, int fd, int64_t off)
{
	if (!len) return -EINVAL;
	len = (len + tux_page - 1) & ~(tux_page - 1);
	int hf = 0;
	if (flags & 0x10) hf |= MAP_FIXED;
	hf |= (flags & 1) ? MAP_SHARED : MAP_PRIVATE;
	if (flags & 0x4000) hf |= MAP_NORESERVE;
	void *want = (void *)addr;
	if (flags & 0x100000) { hf &= ~MAP_FIXED; }   // MAP_FIXED_NOREPLACE
	if (flags & 0x20) {
		void *p = mmap(want, len, prot, hf | MAP_ANON, -1, 0);
		if (p == MAP_FAILED) return ERR();
		if ((flags & 0x100000) && addr && p != want) { munmap(p, len); return -EEXIST; }
		return (long)p;
	}
	if ((flags & 1) && (prot & PROT_WRITE)) {
		// geteilte beschreibbare Abbildung: Darwin macht sie selbst
		if (off & (tux_page - 1)) return -EINVAL;
		void *p = mmap(want, len, prot, hf, fd, off);
		return p == MAP_FAILED ? ERR() : (long)p;
	}
	// Dateiabbildung: Seiten sind hier größer als bei Linux, der Versatz passt oft nicht; deshalb
	// anonymen Speicher anlegen, die Datei hineinlesen und (bei Code) die Systemaufruf-Befehle umschreiben
	void *p = mmap(want, len, PROT_READ | PROT_WRITE, (hf & ~MAP_SHARED) | MAP_PRIVATE | MAP_ANON, -1, 0);
	if (p == MAP_FAILED) return ERR();
	if ((flags & 0x100000) && addr && p != want) { munmap(p, len); return -EEXIST; }
	size_t got = 0;
	while (got < len) {
		ssize_t n = pread(fd, (char *)p + got, len - got, off + got);
		if (n < 0) { int e = errno; munmap(p, len); errno = e; return ERR(); }
		if (n == 0) break;
		got += n;
	}
	if (prot & PROT_EXEC) tux_patch_range(p, got);
	if (mprotect(p, len, prot) != 0) { int e = errno; munmap(p, len); errno = e; return ERR(); }
	return (long)p;
}

static long sys_mprotect(uint64_t addr, uint64_t len, int prot)
{
	if (addr & (tux_page - 1)) return -EINVAL;
	len = (len + tux_page - 1) & ~(tux_page - 1);
	if (prot & PROT_EXEC) {
		// Code, der erst jetzt ausführbar wird, umschreiben
		mprotect((void *)addr, len, PROT_READ | PROT_WRITE);
		tux_patch_range((void *)addr, len);
	}
	return E(mprotect((void *)addr, len, prot));
}

static long fcntl_l2d(int fd, long cmd, long arg)
{
	switch (cmd) {
	case 0: return E(fcntl(fd, F_DUPFD, (int)arg));
	case 1: return E(fcntl(fd, F_GETFD));
	case 2: return E(fcntl(fd, F_SETFD, (int)arg));
	case 1030: return E(fcntl(fd, F_DUPFD_CLOEXEC, (int)arg));
	case 3: {
		long f = fcntl(fd, F_GETFL);
		if (f < 0) return ERR();
		long l = f & 3;
		if (f & O_NONBLOCK) l |= 04000;
		if (f & O_APPEND) l |= 02000;
		return l;
	}
	case 4: {
		int d = 0;
		if (arg & 04000) d |= O_NONBLOCK;
		if (arg & 02000) d |= O_APPEND;
		return E(fcntl(fd, F_SETFL, d));
	}
	case 5: case 6: case 7: return 0;   // Sperren: so tun, als gäbe es sie
	case 1031: case 1032: return 65536;  // F_SETPIPE_SZ, F_GETPIPE_SZ
	default: return -EINVAL;
	}
}

static void tv_from_l(const int64_t *l, struct timeval *d) { d->tv_sec = l[0]; d->tv_usec = l[1]; }
static void tv_to_l(const struct timeval *d, int64_t *l) { l[0] = d->tv_sec; l[1] = d->tv_usec; }

long tux_syscall(tux_regs *r)
{
	uint64_t a0 = r->a[0], a1 = r->a[1], a2 = r->a[2], a3 = r->a[3], a4 = r->a[4], a5 = r->a[5];
	long nr = (long)r->nr;
	if (tux_trace > 1) fprintf(stderr, "tux: syscall %ld(%llx, %llx, %llx)\n", nr, (unsigned long long)a0,
	    (unsigned long long)a1, (unsigned long long)a2);
	switch (nr) {
	case N_read: return E(read((int)a0, (void *)a1, a2));
	case N_write: return E(write((int)a0, (void *)a1, a2));
	case N_readv: return E(readv((int)a0, (void *)a1, (int)a2));
	case N_writev: return E(writev((int)a0, (void *)a1, (int)a2));
	case N_pread64: return E(pread((int)a0, (void *)a1, a2, a3));
	case N_pwrite64: return E(pwrite((int)a0, (void *)a1, a2, a3));
	case N_lseek: return E(lseek((int)a0, a1, (int)a2));
	case N_close: return E(close((int)a0));
	case N_dup: return E(dup((int)a0));
#ifdef N_dup2
	case N_dup2: return E(dup2((int)a0, (int)a1));
#endif
	case N_dup3: {
		if (a0 == a1) return -EINVAL;
		long n = E(dup2((int)a0, (int)a1));
		if (n >= 0 && (a2 & 02000000)) fcntl((int)a1, F_SETFD, FD_CLOEXEC);
		return n;
	}
	case N_fcntl: return fcntl_l2d((int)a0, a1, a2);
	case N_ioctl: return do_ioctl((int)a0, a1, (void *)a2);
	case N_flock: return E(flock((int)a0, (int)a1));
	case N_fsync: case N_fdatasync: return E(fsync((int)a0));
	case N_sync: sync(); return 0;
	case N_ftruncate: return E(ftruncate((int)a0, a1));
	case N_fchmod: return E(fchmod((int)a0, a1));
	case N_fchown: return E(fchown((int)a0, a1, a2));
	case N_fchdir: {
		char hp[PATH_MAX];
		if (fcntl((int)a0, F_GETPATH, hp) < 0) return -EBADF;
		host_to_guest(hp, tux_cwd);
		return 0;
	}
	case N_chdir: {
		char g[PATH_MAX];
		PATH_(AT_FDCWD_L, a0, 1);
		struct stat st;
		if (stat(host, &st) != 0) return ERR();
		if (!S_ISDIR(st.st_mode)) return -ENOTDIR;
		tux_resolve(AT_FDCWD_L, (const char *)a0, 1, host, g);
		strlcpy(tux_cwd, g, sizeof tux_cwd);
		return 0;
	}
	case N_getcwd: {
		size_t n = strlen(tux_cwd) + 1;
		if (n > a1) return -ERANGE;
		memcpy((void *)a0, tux_cwd, n);
		return n;
	}
#ifdef N_open
	case N_open: { PATH_(AT_FDCWD_L, a0, !(a1 & L_O_NOFOLLOW)); return E(open(host, open_flags(a1), (mode_t)a2)); }
	case N_creat: { PATH_(AT_FDCWD_L, a0, 1); return E(open(host, O_WRONLY | O_CREAT | O_TRUNC, (mode_t)a1)); }
	case N_access: { PATH_(AT_FDCWD_L, a0, 1); return E(access(host, (int)a1)); }
	case N_stat: { PATH_(AT_FDCWD_L, a0, 1); struct stat s; if (stat(host, &s)) return ERR(); conv_stat(&s, (void *)a1); return 0; }
	case N_lstat: { PATH_(AT_FDCWD_L, a0, 0); struct stat s; if (lstat(host, &s)) return ERR(); conv_stat(&s, (void *)a1); return 0; }
	case N_mkdir: { PATH_(AT_FDCWD_L, a0, 0); return E(mkdir(host, (mode_t)a1)); }
	case N_rmdir: { PATH_(AT_FDCWD_L, a0, 0); return E(rmdir(host)); }
	case N_unlink: { PATH_(AT_FDCWD_L, a0, 0); return E(unlink(host)); }
	case N_chmod: { PATH_(AT_FDCWD_L, a0, 1); return E(chmod(host, (mode_t)a1)); }
	case N_chown: { PATH_(AT_FDCWD_L, a0, 1); return E(chown(host, a1, a2)); }
	case N_lchown: { PATH_(AT_FDCWD_L, a0, 0); return E(lchown(host, a1, a2)); }
	case N_rename: {
		PATH_(AT_FDCWD_L, a0, 0);
		char h2[PATH_MAX];
		long e = tux_resolve(AT_FDCWD_L, (const char *)a1, 0, h2, NULL);
		if (e < 0) return e;
		return E(rename(host, h2));
	}
	case N_link: {
		PATH_(AT_FDCWD_L, a0, 0);
		char h2[PATH_MAX];
		long e = tux_resolve(AT_FDCWD_L, (const char *)a1, 0, h2, NULL);
		if (e < 0) return e;
		return E(link(host, h2));
	}
	case N_symlink: { PATH_(AT_FDCWD_L, a1, 0); return E(symlink((const char *)a0, host)); }
	case N_readlink: goto readlink_common;
	case N_pipe: {
		int fds[2];
		if (pipe(fds)) return ERR();
		((int *)a0)[0] = fds[0];
		((int *)a0)[1] = fds[1];
		return 0;
	}
	case N_poll: {
		long n = poll((struct pollfd *)a0, (nfds_t)a1, (int)a2);
		return E(n);
	}
	case N_select: goto select_common;
	case N_getdents: return -ENOSYS;
	case N_fork: case N_vfork: {
		pid_t p = fork();
		return p < 0 ? ERR() : p;
	}
	case N_pause: { sigset_t s; sigemptyset(&s); sigsuspend(&s); return -EINTR; }
	case N_alarm: return alarm((unsigned)a0);
	case N_getpgrp: return getpgrp();
	case N_time: { time_t t = time(NULL); if (a0) *(int64_t *)a0 = t; return t; }
	case N_utimes: return -ENOSYS;
	case N_arch_prctl: return -EINVAL;
#endif
	case N_openat: { PATH_(a0, a1, !(a2 & L_O_NOFOLLOW)); return E(open(host, open_flags(a2), (mode_t)a3)); }
	case N_faccessat: case N_faccessat2: { PATH_(a0, a1, !(a3 & 0x100)); return E(access(host, (int)a2)); }
	case N_mkdirat: { PATH_(a0, a1, 0); return E(mkdir(host, (mode_t)a2)); }
	case N_mknodat: {
		PATH_(a0, a1, 0);
		if ((a2 & S_IFMT) == S_IFIFO) return E(mkfifo(host, a2 & 07777));
		return E(mknod(host, a2, a3));
	}
	case N_unlinkat: { PATH_(a0, a1, 0); return E((a2 & 0x200) ? rmdir(host) : unlink(host)); }
	case N_symlinkat: { PATH_(a1, a2, 0); return E(symlink((const char *)a0, host)); }
	case N_linkat: {
		PATH_(a0, a1, 0);
		char h2[PATH_MAX];
		long e = tux_resolve((int)a2, (const char *)a3, 0, h2, NULL);
		if (e < 0) return e;
		return E(link(host, h2));
	}
	case N_renameat: case N_renameat2: {
		PATH_(a0, a1, 0);
		char h2[PATH_MAX];
		long e = tux_resolve((int)a2, (const char *)a3, 0, h2, NULL);
		if (e < 0) return e;
		if (nr == N_renameat2 && a4) {
			if (a4 & 1) { struct stat s; if (lstat(h2, &s) == 0) return -EEXIST; }   // RENAME_NOREPLACE
			else return -EINVAL;
		}
		return E(rename(host, h2));
	}
	case N_fchmodat: { PATH_(a0, a1, !(a3 & 0x100)); return E(chmod(host, (mode_t)a2)); }
	case N_fchownat: { PATH_(a0, a1, !(a4 & 0x100)); return E(lchown(host, a2, a3)); }
	case N_newfstatat: {
		struct stat s;
		int rc;
		if ((a3 & 0x1000) && !*(const char *)a1) rc = fstat((int)a0, &s);
		else { PATH_(a0, a1, !(a3 & 0x100)); rc = (a3 & 0x100) ? lstat(host, &s) : stat(host, &s); }
		if (rc) return ERR();
		conv_stat(&s, (void *)a2);
		return 0;
	}
	case N_statx: {
		struct stat s;
		if ((a2 & 0x1000) && !*(const char *)a1) { if (fstat((int)a0, &s)) return ERR(); }
		else { PATH_(a0, a1, !(a2 & 0x100)); if ((a2 & 0x100) ? lstat(host, &s) : stat(host, &s)) return ERR(); }
		conv_statx(&s, (void *)a4);
		return 0;
	}
	case N_fstat: { struct stat s; if (fstat((int)a0, &s)) return ERR(); conv_stat(&s, (void *)a1); return 0; }
	case N_readlinkat:
	readlink_common: {
		long dirfd = nr == N_readlinkat ? (long)a0 : AT_FDCWD_L;
		const char *path = (const char *)(nr == N_readlinkat ? a1 : a0);
		char *buf = (char *)(nr == N_readlinkat ? a2 : a1);
		size_t sz = nr == N_readlinkat ? a3 : a2;
		if (!strcmp(path, "/proc/self/exe")) {
			size_t n = strlen(tux_exe);
			if (n > sz) n = sz;
			memcpy(buf, tux_exe, n);
			return n;
		}
		// /proc/self/fd/N (und /proc/<pid>/fd/N): Pfad der Datei hinter dem Deskriptor, z. B. für ttyname
		int fdn;
		if ((sscanf(path, "/proc/self/fd/%d", &fdn) == 1) || (sscanf(path, "/proc/%*d/fd/%d", &fdn) == 1)) {
			char hp[PATH_MAX], gp[PATH_MAX];
			if (fcntl(fdn, F_GETPATH, hp) < 0) return -ENOENT;
			host_to_guest(hp, gp);
			size_t n = strlen(gp);
			if (n > sz) n = sz;
			memcpy(buf, gp, n);
			return n;
		}
		PATH_(dirfd, path, 0);
		return E(readlink(host, buf, sz));
	}
	case N_truncate: { PATH_(AT_FDCWD_L, a0, 1); return E(truncate(host, a1)); }
	case N_statfs: case N_fstatfs: {
		struct statfs s;
		int rc;
		if (nr == N_statfs) { PATH_(AT_FDCWD_L, a0, 1); rc = statfs(host, &s); } else rc = fstatfs((int)a0, &s);
		if (rc) return ERR();
		struct { int64_t type, bsize, blocks, bfree, bavail, files, ffree; int32_t fsid[2]; int64_t namelen, frsize, flags, spare[4]; } *l = (void *)a1;
		memset(l, 0, sizeof *l);
		l->type = 0xEF53;
		l->bsize = s.f_bsize;
		l->blocks = s.f_blocks;
		l->bfree = s.f_bfree;
		l->bavail = s.f_bavail;
		l->files = s.f_files;
		l->ffree = s.f_ffree;
		l->namelen = 255;
		l->frsize = s.f_bsize;
		return 0;
	}
	case N_utimensat: {
		struct timespec ts[2];
		struct timespec *tp = NULL;
		if (a2) {
			memcpy(ts, (void *)a2, sizeof ts);
			for (int i = 0; i < 2; i++) {
				if (ts[i].tv_nsec == ((1 << 30) - 1)) ts[i].tv_nsec = UTIME_NOW;
				else if (ts[i].tv_nsec == ((1 << 30) - 2)) ts[i].tv_nsec = UTIME_OMIT;
			}
			tp = ts;
		}
		if (!a1) return E(futimens((int)a0, tp));
		PATH_(a0, a1, !(a3 & 0x100));
		return E(utimensat(AT_FDCWD, host, tp, (a3 & 0x100) ? AT_SYMLINK_NOFOLLOW : 0));
	}
	case N_getdents64: return do_getdents64((int)a0, (void *)a1, a2);
	case N_pipe2: {
		int fds[2];
		if (pipe(fds)) return ERR();
		if (a1 & 02000000) { fcntl(fds[0], F_SETFD, FD_CLOEXEC); fcntl(fds[1], F_SETFD, FD_CLOEXEC); }
		if (a1 & 04000) { fcntl(fds[0], F_SETFL, O_NONBLOCK); fcntl(fds[1], F_SETFL, O_NONBLOCK); }
		((int *)a0)[0] = fds[0];
		((int *)a0)[1] = fds[1];
		return 0;
	}
	case N_close_range: {
		for (unsigned long f = a0; f <= a1 && f < 1024; f++) close((int)f);
		return 0;
	}
	case N_ppoll: {
		int ms = -1;
		if (a2) { const struct timespec *t = (void *)a2; ms = t->tv_sec * 1000 + (t->tv_nsec + 999999) / 1000000; }
		return E(poll((struct pollfd *)a0, (nfds_t)a1, ms));
	}
	case N_pselect6:
	select_common: {
		struct timeval tv, *tvp = NULL;
		if (nr == N_pselect6 && a4) {
			const struct timespec *t = (void *)a4;
			tv.tv_sec = t->tv_sec;
			tv.tv_usec = (t->tv_nsec + 999) / 1000;
			tvp = &tv;
		} else if (nr != N_pselect6 && a4) {
			tv_from_l((int64_t *)a4, &tv);
			tvp = &tv;
		}
		return E(select((int)a0, (void *)a1, (void *)a2, (void *)a3, tvp));
	}
	case N_sendfile: {
		char b[16384];
		size_t tot = 0, want = a3;
		while (tot < want) {
			ssize_t n = read((int)a1, b, want - tot < sizeof b ? want - tot : sizeof b);
			if (n < 0) return tot ? (long)tot : ERR();
			if (!n) break;
			ssize_t w = write((int)a0, b, n);
			if (w < 0) return tot ? (long)tot : ERR();
			tot += w;
		}
		return tot;
	}
	// Speicher
	case N_mmap: return sys_mmap(a0, a1, (int)a2, (int)a3, (int)a4, a5);
	case N_munmap: return E(munmap((void *)a0, (a1 + tux_page - 1) & ~(tux_page - 1)));
	case N_mprotect: return sys_mprotect(a0, a1, (int)a2);
	case N_mremap: {
		size_t os = (a1 + tux_page - 1) & ~(tux_page - 1), ns = (a2 + tux_page - 1) & ~(tux_page - 1);
		if (ns <= os) { if (ns < os) munmap((char *)a0 + ns, os - ns); return a0; }
		if (!(a3 & 1)) return -ENOMEM;
		void *p = mmap(NULL, ns, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
		if (p == MAP_FAILED) return ERR();
		memcpy(p, (void *)a0, os);
		munmap((void *)a0, os);
		return (long)p;
	}
	case N_msync: return E(msync((void *)a0, a1, (int)a2));
	case N_madvise: return 0;
	case N_brk: {
		if (!brk_base) return (long)brk_cur;
		uint8_t *want = (uint8_t *)a0;
		if (!want || want < brk_base || want > brk_end) return (long)brk_cur;
		if (want < brk_cur) memset(want, 0, brk_cur - want);
		brk_cur = want;
		return (long)brk_cur;
	}
	// Prozesse und Identität
	case N_getpid: return getpid();
	case N_getppid: return getppid();
	case N_gettid: return getpid();
	case N_getuid: return getuid();
	case N_geteuid: return geteuid();
	case N_getgid: return getgid();
	case N_getegid: return getegid();
	case N_setuid: return E(setuid(a0));
	case N_setgid: return E(setgid(a0));
	case N_setreuid: return E(setreuid(a0, a1));
	case N_setregid: return E(setregid(a0, a1));
	case N_setpgid: return E(setpgid(a0, a1));
	case N_getpgid: return E(getpgid(a0));
	case N_getsid: return E(getsid(a0));
	case N_setsid: return E(setsid());
	case N_getgroups: {
		gid_t g[NGROUPS_MAX];
		int n = getgroups(NGROUPS_MAX, g);
		if (n < 0) return ERR();
		if (a0 == 0) return n;
		if ((long)a0 < n) return -EINVAL;
		memcpy((void *)a1, g, n * sizeof(gid_t));
		return n;
	}
	case N_setgroups: return 0;
	case N_umask: return umask(a0);
	case N_set_tid_address: return getpid();
	case N_set_robust_list: return 0;
	case N_rseq: return -ENOSYS;
	case N_sched_yield: sched_yield(); return 0;
	case N_sched_getaffinity: {
		if (a1 < 8) return -EINVAL;
		memset((void *)a2, 0, a1);
		*(uint8_t *)a2 = 1;
		return 8;
	}
	case N_sched_setaffinity: return 0;
	case N_getpriority: { errno = 0; int p = getpriority((int)a0, a1); return errno ? ERR() : 20 - p; }
	case N_setpriority: return E(setpriority((int)a0, a1, (int)a2));
	case N_prctl: {
		switch (a0) {
		case 15: return 0;                                  // PR_SET_NAME
		case 16: memcpy((void *)a1, "tux\0\0\0\0\0", 8); return 0;   // PR_GET_NAME
		case 1: case 36: case 38: return 0;                // PDEATHSIG, CHILD_SUBREAPER, NO_NEW_PRIVS
		default: return -EINVAL;
		}
	}
	case N_clone: {
		unsigned long fl = a0;
		if (fl & 0x10000) {   // CLONE_THREAD
			if (tux_trace) fprintf(stderr, "tux: clone mit Threads (noch nicht übersetzt)\n");
			return -ENOSYS;
		}
		uint64_t ptid = a2;
		pid_t p = fork();
		if (p < 0) return ERR();
		if (p == 0) {
			if (a1) *r->sp = a1;
			return 0;
		}
		if ((fl & 0x100) && ptid) *(int *)ptid = p;   // CLONE_PARENT_SETTID
		return p;
	}
	case N_clone3: return -ENOSYS;
	case N_execve: {
		char **argv = (char **)a1, **envp = (char **)a2;
		return do_execve((const char *)a0, argv, envp);
	}
	case N_wait4: {
		int st = 0;
		int opt = 0;
		if (a2 & 1) opt |= WNOHANG;
		if (a2 & 2) opt |= WUNTRACED;
		if (a2 & 8) opt |= WCONTINUED;
		struct rusage ru;
		pid_t p = wait4((pid_t)a0, &st, opt, &ru);
		if (p < 0) return ERR();
		if (a1) *(int *)a1 = sig_status_d2l(st);
		if (a3) memset((void *)a3, 0, 144);
		return p;
	}
	case N_exit: case N_exit_group: _exit((int)a0);
	case N_kill: {
		int s = (int)a1;
		if (s) { s = tux_sig_l2d(s); if (s < 0) return -EINVAL; }
		return E(kill((pid_t)a0, s));
	}
	case N_tkill: case N_tgkill: {
		long pid = nr == N_tgkill ? a0 : getpid();
		long sig = nr == N_tgkill ? a2 : a1;
		int s = sig ? tux_sig_l2d((int)sig) : 0;
		if (sig && s < 0) return -EINVAL;
		return E(kill((pid_t)pid, s));
	}
	case N_rt_sigaction: return tux_sigaction(a0, (void *)a1, (void *)a2, a3);
	case N_rt_sigprocmask: return tux_sigprocmask(a0, (const uint64_t *)a1, (uint64_t *)a2, a3);
	case N_rt_sigpending: {
		sigset_t s;
		if (sigpending(&s)) return ERR();
		uint64_t l = 0;
		for (int i = 1; i < 32; i++) if (sigismember(&s, i)) { int ls = tux_sig_d2l(i); if (ls > 0) l |= 1ULL << (ls - 1); }
		*(uint64_t *)a0 = l;
		return 0;
	}
	case N_rt_sigsuspend: {
		sigset_t s;
		tux_guest_to_host_sigset(*(uint64_t *)a0, &s);
		sigsuspend(&s);
		return -EINTR;
	}
	case N_sigaltstack: return 0;
	case N_rt_sigreturn: return 0;
	// Zeit
	case N_clock_gettime: {
		int c = clock_l2d(a0);
		if (c < 0) return -EINVAL;
		return E(clock_gettime((clockid_t)c, (struct timespec *)a1));
	}
	case N_clock_getres: {
		int c = clock_l2d(a0);
		if (c < 0) return -EINVAL;
		if (!a1) return 0;
		{ struct timespec *t = (void *)a1; t->tv_sec = 0; t->tv_nsec = 1000; return 0; }   /* PureDarwins libSystem hat kein clock_getres */
	}
	case N_gettimeofday: {
		struct timeval tv;
		if (gettimeofday(&tv, NULL)) return ERR();
		if (a0) tv_to_l(&tv, (int64_t *)a0);
		if (a1) memset((void *)a1, 0, 8);
		return 0;
	}
	case N_nanosleep: {
		struct timespec rem;
		int rc = nanosleep((struct timespec *)a0, &rem);
		if (rc && errno == EINTR && a1) *(struct timespec *)a1 = rem;
		return E(rc);
	}
	case N_clock_nanosleep: {
		int c = clock_l2d(a0);
		struct timespec t = *(struct timespec *)a2, rem;
		if (a1 & 1) {   // TIMER_ABSTIME
			struct timespec now;
			clock_gettime(c < 0 ? CLOCK_REALTIME : c, &now);
			t.tv_sec -= now.tv_sec;
			t.tv_nsec -= now.tv_nsec;
			if (t.tv_nsec < 0) { t.tv_nsec += 1000000000; t.tv_sec--; }
			if (t.tv_sec < 0) return 0;
		}
		int rc = nanosleep(&t, &rem);
		if (rc && errno == EINTR) { if (a3 && !(a1 & 1)) *(struct timespec *)a3 = rem; return -EINTR; }
		return E(rc);
	}
	case N_setitimer: case N_getitimer: {
		struct itimerval nv, ov;
		if (nr == N_getitimer) {
			return -ENOSYS;   /* getitimer fehlt in PureDarwins libSystem */
			int64_t *o = (int64_t *)a1;
			tv_to_l(&ov.it_interval, o);
			tv_to_l(&ov.it_value, o + 2);
			return 0;
		}
		if (a1) {
			int64_t *n = (int64_t *)a1;
			tv_from_l(n, &nv.it_interval);
			tv_from_l(n + 2, &nv.it_value);
		} else memset(&nv, 0, sizeof nv);
		if (setitimer((int)a0, &nv, &ov)) return ERR();
		if (a2) { int64_t *o = (int64_t *)a2; tv_to_l(&ov.it_interval, o); tv_to_l(&ov.it_value, o + 2); }
		return 0;
	}
	case N_times: {
		struct tms t;
		clock_t c = times(&t);
		if (c == (clock_t)-1) return ERR();
		if (a0) { int64_t *o = (int64_t *)a0; o[0] = t.tms_utime; o[1] = t.tms_stime; o[2] = t.tms_cutime; o[3] = t.tms_cstime; }
		return c;
	}
	case N_getrusage: {
		struct rusage ru;
		if (getrusage((int)a0 == -1 ? RUSAGE_CHILDREN : RUSAGE_SELF, &ru)) return ERR();
		int64_t *o = (int64_t *)a1;
		memset(o, 0, 144);
		tv_to_l(&ru.ru_utime, o);
		tv_to_l(&ru.ru_stime, o + 2);
		o[4] = ru.ru_maxrss / 1024;
		return 0;
	}
	// Rest
	case N_uname: {
		struct { char s[65], n[65], r[65], v[65], m[65], d[65]; } *u = (void *)a0;
		memset(u, 0, sizeof *u);
		strcpy(u->s, "Linux");
		gethostname(u->n, 64);
		strcpy(u->r, "6.1.0-tux");
		strcpy(u->v, "#1 ClokeOS tux");
		strcpy(u->m, TUX_NAME);
		strcpy(u->d, "(none)");
		return 0;
	}
	case N_sysinfo: {
		int64_t *o = (int64_t *)a0;
		memset(o, 0, 112);
		uint64_t mem = 0;
		size_t l = sizeof mem;
		sysctlbyname("hw.memsize", &mem, &l, NULL, 0);
		struct timeval bt;
		l = sizeof bt;
		int mib[2] = {CTL_KERN, KERN_BOOTTIME};
		if (sysctl(mib, 2, &bt, &l, NULL, 0) == 0) o[0] = time(NULL) - bt.tv_sec;
		o[2] = mem / 2; o[3] = mem / 2;
		((uint32_t *)o)[26] = 1;
		o[13] = 1;   // mem_unit liegt später; grob genug
		return 0;
	}
	case N_getrandom: {
		uint8_t *b = (uint8_t *)a0;
		size_t n = a1, done = 0;
		while (done < n) {
			size_t c = n - done > 256 ? 256 : n - done;
			if (getentropy(b + done, c)) return ERR();
			done += c;
		}
		return n;
	}
	case N_getrlimit: case N_prlimit64: case N_setrlimit: {
		long res = nr == N_prlimit64 ? (long)a1 : (long)a0;
		uint64_t *newp = (uint64_t *)(nr == N_prlimit64 ? a2 : nr == N_setrlimit ? a1 : 0);
		uint64_t *oldp = (uint64_t *)(nr == N_prlimit64 ? a3 : nr == N_getrlimit ? a1 : 0);
		static const int map[] = {RLIMIT_CPU, RLIMIT_FSIZE, RLIMIT_DATA, RLIMIT_STACK, RLIMIT_CORE, RLIMIT_RSS,
		    RLIMIT_NPROC, RLIMIT_NOFILE, RLIMIT_MEMLOCK, RLIMIT_AS};
		if (res < 0 || res >= (long)(sizeof map / sizeof *map)) {
			if (oldp) { oldp[0] = oldp[1] = ~0ULL; }
			return 0;
		}
		struct rlimit rl;
		if (getrlimit(map[res], &rl)) return ERR();
		if (oldp) {
			oldp[0] = rl.rlim_cur == RLIM_INFINITY ? ~0ULL : rl.rlim_cur;
			oldp[1] = rl.rlim_max == RLIM_INFINITY ? ~0ULL : rl.rlim_max;
		}
		if (newp) {
			rl.rlim_cur = newp[0] == ~0ULL ? RLIM_INFINITY : newp[0];
			rl.rlim_max = newp[1] == ~0ULL ? RLIM_INFINITY : newp[1];
			if (setrlimit(map[res], &rl)) return ERR();
		}
		return 0;
	}
	case N_futex: {
		int op = (int)a1 & 0x7f;
		if (op == 0 /* WAIT */) {
			// ohne weitere Threads kann niemand wecken: wenn der Wert noch gilt, nur kurz schlafen
			if (*(volatile int *)a0 != (int)a3) return -EAGAIN;
			struct timespec t = {0, 1000000};
			nanosleep(&t, NULL);
			return -EAGAIN;
		}
		return 0;   // WAKE & Co: niemand wartet
	}
	case N_fallocate: return -EOPNOTSUPP;
	case N_eventfd2: case N_epoll_create1: return -ENOSYS;
	default:
		if (tux_trace) fprintf(stderr, "tux: Systemaufruf %ld nicht übersetzt (a0=%llx)\n", nr, (unsigned long long)a0);
		return -ENOSYS;
	}
}
