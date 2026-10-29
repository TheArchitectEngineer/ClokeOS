// tux – Sockets: Adressen, Optionen, Flags und msghdr zwischen Linux- und Darwin-Darstellung umrechnen.
// SOCK_SEQPACKET gibt es unter Darwin nicht (AF_UNIX): es läuft über einen Stromsocket, jede Nachricht trägt
// vorn vier Byte Länge (Rahmen); read/write/send/recv des Gastes gehen dann durch tux_seq_*.
#include "tux.h"
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/uio.h>
#include <sys/ucred.h>
#include <unistd.h>

static long err(void) { return -tux_errno(errno); }

#define L_O_NONBLOCK 04000
#define L_SOCK_CLOEXEC 02000000
#define ABS_DIR "/private/tmp/.tux-abstract"

// --- Domänen und Adressen ---------------------------------------------------------------------
static int dom_l2d(int d) { return d == 1 ? AF_UNIX : d == 2 ? AF_INET : d == 10 ? AF_INET6 : -1; }
static int fam_d2l(int f) { return f == AF_UNIX ? 1 : f == AF_INET ? 2 : f == AF_INET6 ? 10 : f; }

// Abstrakte Namen (Linux: erstes Byte NUL) liegen als Dateien unter /private/tmp/.tux-abstract/<Name mit %XX>
static void abs_to_path(const uint8_t *name, size_t n, char *out, size_t cap)
{
	mkdir(ABS_DIR, 01777);
	size_t o = snprintf(out, cap, "%s/", ABS_DIR);
	for (size_t i = 0; i < n && o + 4 < cap; i++) {
		uint8_t c = name[i];
		if ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-')
			out[o++] = c;
		else o += snprintf(out + o, cap - o, "%%%02x", c);
	}
	out[o] = 0;
}

static int path_to_abs(const char *p, uint8_t *out, size_t cap)   // Länge ohne führendes NUL, oder -1
{
	size_t dl = strlen(ABS_DIR);
	if (strncmp(p, ABS_DIR "/", dl + 1)) return -1;
	size_t o = 0;
	for (p += dl + 1; *p && o + 1 < cap; p++) {
		if (*p == '%' && p[1] && p[2]) { unsigned v; sscanf(p + 1, "%2x", &v); out[o++] = (uint8_t)v; p += 2; }
		else out[o++] = (uint8_t)*p;
	}
	return (int)o;
}

extern void tux_host_to_guest(const char *h, char *g);

// Linux-Adresse -> Darwin. Rückgabe 0 oder -errno
static int addr_l2d(const void *lv, socklen_t ll, struct sockaddr_storage *d, socklen_t *dl, int follow)
{
	const uint8_t *l = lv;
	if (ll < 2) return -tux_errno(EINVAL);
	int fam = *(const uint16_t *)l;
	memset(d, 0, sizeof *d);
	switch (fam) {
	case 1: {
		struct sockaddr_un *u = (struct sockaddr_un *)d;
		u->sun_family = AF_UNIX;
		size_t n = ll - 2;
		if (n > 108) n = 108;
		char host[PATH_MAX];
		if (n == 0) { u->sun_len = 2; *dl = 2; return 0; }   // unbenannt
		if (l[2] == 0) abs_to_path(l + 3, n - 1, host, sizeof host);
		else {
			char gp[110];
			memcpy(gp, l + 2, n);
			gp[n] = 0;
			int r = tux_resolve(-100, gp, follow, host, NULL);
			if (r < 0) return r;
		}
		if (strlen(host) >= sizeof u->sun_path) return -tux_errno(ENAMETOOLONG);
		strcpy(u->sun_path, host);
		u->sun_len = (uint8_t)(offsetof(struct sockaddr_un, sun_path) + strlen(host) + 1);
		*dl = u->sun_len;
		return 0;
	}
	case 2: {
		if (ll < 8) return -tux_errno(EINVAL);
		struct sockaddr_in *s = (struct sockaddr_in *)d;
		s->sin_len = sizeof *s;
		s->sin_family = AF_INET;
		memcpy(&s->sin_port, l + 2, 2);
		memcpy(&s->sin_addr, l + 4, 4);
		*dl = sizeof *s;
		return 0;
	}
	case 10: {
		if (ll < 24) return -tux_errno(EINVAL);
		struct sockaddr_in6 *s = (struct sockaddr_in6 *)d;
		s->sin6_len = sizeof *s;
		s->sin6_family = AF_INET6;
		memcpy(&s->sin6_port, l + 2, 2);
		memcpy(&s->sin6_flowinfo, l + 4, 4);
		memcpy(&s->sin6_addr, l + 8, 16);
		if (ll >= 28) memcpy(&s->sin6_scope_id, l + 24, 4);
		*dl = sizeof *s;
		return 0;
	}
	default: return -tux_errno(EAFNOSUPPORT);
	}
}

// Darwin-Adresse -> Linux, kopiert höchstens cap Byte; Rückgabe: volle Länge
static socklen_t addr_d2l(const struct sockaddr *d, socklen_t dl, void *out, socklen_t cap)
{
	uint8_t tmp[128];
	memset(tmp, 0, sizeof tmp);
	socklen_t n = 0;
	if (dl < 2) { n = 0; }
	else if (d->sa_family == AF_UNIX) {
		const struct sockaddr_un *u = (const struct sockaddr_un *)d;
		*(uint16_t *)tmp = 1;
		size_t pl = dl > offsetof(struct sockaddr_un, sun_path) ? strnlen(u->sun_path, dl - offsetof(struct sockaddr_un, sun_path)) : 0;
		if (pl) {
			char g[PATH_MAX];
			uint8_t nm[110];
			int an = path_to_abs(u->sun_path, nm, sizeof nm);
			if (an >= 0) { tmp[2] = 0; memcpy(tmp + 3, nm, an); n = 3 + an; }
			else {
				tux_host_to_guest(u->sun_path, g);
				size_t gl = strlen(g);
				if (gl > 107) gl = 107;
				memcpy(tmp + 2, g, gl);
				n = 2 + gl + 1;
			}
		} else n = 2;
	} else if (d->sa_family == AF_INET) {
		*(uint16_t *)tmp = 2;
		memcpy(tmp + 2, d->sa_data, 6);   // Port, Adresse
		n = 16;
	} else if (d->sa_family == AF_INET6) {
		const struct sockaddr_in6 *s = (const struct sockaddr_in6 *)d;
		*(uint16_t *)tmp = 10;
		memcpy(tmp + 2, &s->sin6_port, 2);
		memcpy(tmp + 4, &s->sin6_flowinfo, 4);
		memcpy(tmp + 8, &s->sin6_addr, 16);
		memcpy(tmp + 24, &s->sin6_scope_id, 4);
		n = 28;
	} else { *(uint16_t *)tmp = (uint16_t)fam_d2l(d->sa_family); n = 2; }
	memcpy(out, tmp, n < cap ? n : cap);
	return n;
}

// --- Flags -----------------------------------------------------------------------------------
static int mflags_l2d(int f)
{
	int d = 0;
	if (f & 1) d |= MSG_OOB;
	if (f & 2) d |= MSG_PEEK;
	if (f & 4) d |= MSG_DONTROUTE;
	if (f & 0x20) d |= MSG_TRUNC;
	if (f & 0x40) d |= MSG_DONTWAIT;
	if (f & 0x80) d |= MSG_EOR;
	if (f & 0x100) d |= MSG_WAITALL;
	return d;
}
static int mflags_d2l(int f)
{
	int l = 0;
	if (f & MSG_OOB) l |= 1;
	if (f & MSG_PEEK) l |= 2;
	if (f & MSG_CTRUNC) l |= 8;
	if (f & MSG_TRUNC) l |= 0x20;
	if (f & MSG_EOR) l |= 0x80;
	return l;
}

// --- Socket-Optionen -------------------------------------------------------------------------
static int opt_l2d(int level, int name, int *dl, int *dn)
{
	static const int so[][2] = {{1, 1}, {2, SO_REUSEADDR}, {3, SO_TYPE}, {4, SO_ERROR}, {5, SO_DONTROUTE}, {6, SO_BROADCAST},
	    {7, SO_SNDBUF}, {8, SO_RCVBUF}, {9, SO_KEEPALIVE}, {10, SO_OOBINLINE}, {13, SO_LINGER}, {15, SO_REUSEPORT},
	    {18, SO_RCVLOWAT}, {19, SO_SNDLOWAT}, {20, SO_RCVTIMEO}, {21, SO_SNDTIMEO}, {30, SO_ACCEPTCONN}, {29, SO_TIMESTAMP}};
	switch (level) {
	case 1:
		*dl = SOL_SOCKET;
		for (unsigned i = 0; i < sizeof so / sizeof *so; i++) if (so[i][0] == name) { *dn = so[i][1]; return 0; }
		return -1;
	case 6:
		*dl = IPPROTO_TCP;
		switch (name) {
		case 1: *dn = TCP_NODELAY; return 0;
		case 2: *dn = TCP_MAXSEG; return 0;
		case 4: *dn = 0x10; return 0;     // TCP_KEEPALIVE (Leerlaufzeit)
		case 5: *dn = 0x101; return 0;    // TCP_KEEPINTVL
		case 6: *dn = 0x102; return 0;    // TCP_KEEPCNT
		}
		return -1;
	case 0:
		*dl = IPPROTO_IP;
		switch (name) {
		case 1: *dn = 3; return 0;   case 2: *dn = 4; return 0;
		case 32: *dn = 9; return 0;  case 33: *dn = 10; return 0; case 34: *dn = 11; return 0;
		case 35: *dn = 12; return 0; case 36: *dn = 13; return 0;
		}
		return -1;
	case 41:
		*dl = IPPROTO_IPV6;
		switch (name) {
		case 16: *dn = 4; return 0;  case 18: *dn = 9; return 0;  case 19: *dn = 10; return 0;
		case 20: *dn = 11; return 0; case 26: *dn = 27; return 0;
		}
		return -1;
	}
	return -1;
}

int tux_fdx_kind(int fd);
void tux_fdx_mark_seqpkt(int fd);
#define K_SEQPKT 5

long tux_setsockopt(int fd, long level, long name, const void *val, socklen_t len)
{
	if (level == 1 && (name == 16 || name == 12 || name == 38 || name == 35)) return 0;   // PASSCRED & Co. gibt es nicht
	if (level == 6 && (name == 3 || name == 12 || name == 13 || name == 18)) return 0;     // CORK, QUICKACK, ...
	int dl, dn;
	if (opt_l2d((int)level, (int)name, &dl, &dn) < 0) return -tux_errno(ENOPROTOOPT);
	if (level == 1 && (name == 20 || name == 21)) {   // timeval: bei Linux 2 x 64 Bit
		if (len < 16) return -tux_errno(EINVAL);
		struct timeval tv = {((const int64_t *)val)[0], (int)((const int64_t *)val)[1]};
		return setsockopt(fd, dl, dn, &tv, sizeof tv) ? err() : 0;
	}
	return setsockopt(fd, dl, dn, val, len) ? err() : 0;
}

long tux_getsockopt(int fd, long level, long name, void *val, socklen_t *len)
{
	if (level == 1 && name == 17) {   // SO_PEERCRED: struct ucred { pid, uid, gid }
		struct xucred xu;
		socklen_t xl = sizeof xu;
		pid_t pid = 0;
		socklen_t pl = sizeof pid;
		if (getsockopt(fd, 0, 1 /* LOCAL_PEERCRED */, &xu, &xl)) return err();
		getsockopt(fd, 0, 2 /* LOCAL_PEERPID */, &pid, &pl);
		int32_t c[3] = {pid, (int32_t)xu.cr_uid, xu.cr_ngroups ? (int32_t)xu.cr_groups[0] : 0};
		socklen_t n = *len < 12 ? *len : 12;
		memcpy(val, c, n);
		*len = 12;
		return 0;
	}
	int dl, dn;
	if (opt_l2d((int)level, (int)name, &dl, &dn) < 0) return -tux_errno(ENOPROTOOPT);
	if (level == 1 && (name == 20 || name == 21)) {
		struct timeval tv;
		socklen_t tl = sizeof tv;
		if (getsockopt(fd, dl, dn, &tv, &tl)) return err();
		int64_t l[2] = {tv.tv_sec, tv.tv_usec};
		socklen_t n = *len < 16 ? *len : 16;
		memcpy(val, l, n);
		*len = 16;
		return 0;
	}
	socklen_t l = *len;
	if (getsockopt(fd, dl, dn, val, &l)) return err();
	*len = l;
	if (level == 1 && name == 3 && *len >= 4) {   // SO_TYPE
		int *t = val;
		if (tux_fdx_kind(fd) == K_SEQPKT) *t = 5;
	}
	if (level == 1 && name == 4 && *len >= 4) { int *e = val; *e = *e ? tux_errno(*e) : 0; }
	return 0;
}

// --- SOCK_SEQPACKET über Stromsocket + Längenrahmen ---------------------------------------------
static int wait_fd(int fd, int ev)
{
	struct pollfd p = {fd, (short)ev, 0};
	return poll(&p, 1, -1);
}
static int is_nonblock(int fd) { int f = fcntl(fd, F_GETFL); return f >= 0 && (f & O_NONBLOCK); }

static int send_all(int fd, const uint8_t *b, size_t n)
{
	while (n) {
		ssize_t w = send(fd, b, n, 0);
		if (w < 0) {
			if (errno == EINTR) continue;
			if (errno == EAGAIN) { wait_fd(fd, POLLOUT); continue; }
			return -1;
		}
		b += w; n -= w;
	}
	return 0;
}
static int recv_all(int fd, uint8_t *b, size_t n)
{
	while (n) {
		ssize_t r = recv(fd, b, n, 0);
		if (r == 0) { errno = ECONNRESET; return -1; }
		if (r < 0) {
			if (errno == EINTR) continue;
			if (errno == EAGAIN) { wait_fd(fd, POLLIN); continue; }
			return -1;
		}
		b += r; n -= r;
	}
	return 0;
}

// Sendet eine Nachricht; ctl/ctllen: Darwin-Steuerdaten (SCM_RIGHTS) hängen am ersten Byte
static long seq_send(int fd, const struct iovec *iov, int iovcnt, void *ctl, socklen_t ctllen)
{
	size_t total = 0;
	for (int i = 0; i < iovcnt; i++) total += iov[i].iov_len;
	uint8_t *buf = malloc(4 + total);
	if (!buf) return -tux_errno(ENOMEM);
	uint32_t n32 = (uint32_t)total;
	memcpy(buf, &n32, 4);
	size_t o = 4;
	for (int i = 0; i < iovcnt; i++) { memcpy(buf + o, iov[i].iov_base, iov[i].iov_len); o += iov[i].iov_len; }
	struct iovec one = {buf, 4 + total};
	struct msghdr m;
	memset(&m, 0, sizeof m);
	m.msg_iov = &one;
	m.msg_iovlen = 1;
	m.msg_control = ctl;
	m.msg_controllen = ctllen;
	ssize_t w = sendmsg(fd, &m, 0);
	long ret;
	if (w < 0) ret = err();
	else {
		if ((size_t)w < 4 + total && send_all(fd, buf + w, 4 + total - w) < 0) ret = err();
		else ret = (long)total;
	}
	free(buf);
	return ret;
}

// Empfängt eine Nachricht; *trunc wird 1, wenn sie größer war als der Platz
static long seq_recv(int fd, const struct iovec *iov, int iovcnt, void *ctl, socklen_t *ctllen, int *trunc)
{
	uint32_t n32;
	uint8_t *hb = (uint8_t *)&n32;
	struct iovec hv = {hb, 4};
	struct msghdr m;
	memset(&m, 0, sizeof m);
	m.msg_iov = &hv;
	m.msg_iovlen = 1;
	m.msg_control = ctl;
	m.msg_controllen = ctl ? *ctllen : 0;
	ssize_t r = recvmsg(fd, &m, 0);
	if (r < 0) return err();
	if (ctl) *ctllen = m.msg_controllen;
	if (r == 0) return 0;   // Gegenstelle zu
	if (r < 4 && recv_all(fd, hb + r, 4 - r) < 0) return err();
	uint8_t *body = malloc(n32 ? n32 : 1);
	if (!body) return -tux_errno(ENOMEM);
	if (n32 && recv_all(fd, body, n32) < 0) { long e = err(); free(body); return e; }
	size_t cap = 0, o = 0;
	for (int i = 0; i < iovcnt; i++) cap += iov[i].iov_len;
	size_t take = n32 < cap ? n32 : cap;
	for (int i = 0; i < iovcnt && o < take; i++) {
		size_t c = iov[i].iov_len < take - o ? iov[i].iov_len : take - o;
		memcpy(iov[i].iov_base, body + o, c);
		o += c;
	}
	free(body);
	*trunc = n32 > cap;
	return (long)take;
}

int tux_seq_read(int fd, void *buf, size_t n, long *ret)
{
	struct iovec v = {buf, n};
	int tr;
	*ret = seq_recv(fd, &v, 1, NULL, NULL, &tr);
	return 1;
}
int tux_seq_write(int fd, const void *buf, size_t n, long *ret)
{
	struct iovec v = {(void *)buf, n};
	*ret = seq_send(fd, &v, 1, NULL, 0);
	return 1;
}

// --- msghdr und Steuerdaten --------------------------------------------------------------------
struct l_msghdr { void *name; uint32_t namelen; int pad; struct iovec *iov; uint64_t iovlen; void *control; uint64_t controllen; int flags; int pad2; };
struct l_cmsghdr { uint64_t len; int level; int type; };

// Linux-Steuerdaten -> Darwin (nur SCM_RIGHTS). Rückgabe: malloc'ter Puffer oder NULL, *dlen gesetzt
static void *ctl_l2d(const void *lc, uint64_t ll, socklen_t *dlen)
{
	*dlen = 0;
	if (!lc || ll < sizeof(struct l_cmsghdr)) return NULL;
	uint8_t *out = calloc(1, ll + 64);
	size_t o = 0;
	const uint8_t *p = lc;
	while (ll >= sizeof(struct l_cmsghdr)) {
		const struct l_cmsghdr *h = (const void *)p;
		if (h->len < sizeof *h || h->len > ll) break;
		if (h->level == 1 && h->type == 1) {   // SOL_SOCKET, SCM_RIGHTS
			size_t dl = h->len - sizeof *h;
			struct cmsghdr *c = (struct cmsghdr *)(out + o);
			c->cmsg_len = (socklen_t)(sizeof(struct cmsghdr) + dl);
			c->cmsg_level = SOL_SOCKET;
			c->cmsg_type = SCM_RIGHTS;
			memcpy(CMSG_DATA(c), p + sizeof *h, dl);
			o += CMSG_SPACE(dl);
		}
		size_t adv = (h->len + 7) & ~7UL;
		if (adv >= ll) break;
		p += adv; ll -= adv;
	}
	if (!o) { free(out); return NULL; }
	*dlen = (socklen_t)o;
	return out;
}

// Darwin-Steuerdaten -> Linux-Puffer; Rückgabe: belegte Länge, *ctrunc bei zu kleinem Puffer
static uint64_t ctl_d2l(const void *dc, socklen_t dl, void *lc, uint64_t lcap, int cloexec, int *ctrunc)
{
	uint64_t o = 0;
	if (!dc) return 0;
	struct msghdr m;
	memset(&m, 0, sizeof m);
	m.msg_control = (void *)dc;
	m.msg_controllen = dl;
	for (struct cmsghdr *c = CMSG_FIRSTHDR(&m); c; c = CMSG_NXTHDR(&m, c)) {
		if (c->cmsg_level != SOL_SOCKET || c->cmsg_type != SCM_RIGHTS) continue;
		size_t n = c->cmsg_len - sizeof *c;
		uint64_t need = sizeof(struct l_cmsghdr) + n;
		if (o + need > lcap) { *ctrunc = 1; break; }
		struct l_cmsghdr *h = (struct l_cmsghdr *)((uint8_t *)lc + o);
		h->len = need;
		h->level = 1;
		h->type = 1;
		memcpy((uint8_t *)lc + o + sizeof *h, CMSG_DATA(c), n);
		if (cloexec) for (size_t i = 0; i < n / 4; i++) fcntl(((int *)CMSG_DATA(c))[i], F_SETFD, FD_CLOEXEC);
		o += (need + 7) & ~7UL;
	}
	return o;
}

// --- Sockets ---------------------------------------------------------------------------------
long tux_socket(long domain, long type, long proto)
{
	int d = dom_l2d((int)domain);
	if (d < 0) return -tux_errno(EAFNOSUPPORT);
	int t = type & 0xf;
	int seq = 0;
	if (t == 5) {
		if (d != AF_UNIX) return -tux_errno(ESOCKTNOSUPPORT);
		t = SOCK_STREAM;
		seq = 1;
	} else if (t != 1 && t != 2 && t != 3) return -tux_errno(EINVAL);
	int fd = socket(d, t, (int)proto);
	if (fd < 0) return err();
	if (type & L_O_NONBLOCK) fcntl(fd, F_SETFL, O_NONBLOCK);
	if (type & L_SOCK_CLOEXEC) fcntl(fd, F_SETFD, FD_CLOEXEC);
	if (seq) tux_fdx_mark_seqpkt(fd);
	return fd;
}

long tux_socketpair(long domain, long type, long proto, int *sv)
{
	int d = dom_l2d((int)domain);
	if (d != AF_UNIX) return -tux_errno(EAFNOSUPPORT);
	int t = type & 0xf, seq = 0;
	if (t == 5) { t = SOCK_STREAM; seq = 1; }
	else if (t != 1 && t != 2) return -tux_errno(EINVAL);
	int fds[2];
	if (socketpair(d, t, 0, fds)) return err();
	for (int i = 0; i < 2; i++) {
		if (type & L_O_NONBLOCK) fcntl(fds[i], F_SETFL, O_NONBLOCK);
		if (type & L_SOCK_CLOEXEC) fcntl(fds[i], F_SETFD, FD_CLOEXEC);
		if (seq) tux_fdx_mark_seqpkt(fds[i]);
	}
	sv[0] = fds[0];
	sv[1] = fds[1];
	return 0;
}

long tux_bind(int fd, const void *a, socklen_t al)
{
	struct sockaddr_storage d;
	socklen_t dl;
	int r = addr_l2d(a, al, &d, &dl, 0);
	if (r < 0) return r;
	if (al > 3 && ((const uint8_t *)a)[2] == 0 && ((const uint8_t *)a)[1] == 0) {
		// abstrakter Name: liegt von früher eine Datei da, deren Besitzer weg ist, wird sie ersetzt
		int probe = socket(AF_UNIX, SOCK_STREAM, 0);
		if (probe >= 0) {
			if (connect(probe, (struct sockaddr *)&d, dl) != 0 && errno == ECONNREFUSED) unlink(((struct sockaddr_un *)&d)->sun_path);
			close(probe);
		}
	}
	return bind(fd, (struct sockaddr *)&d, dl) ? err() : 0;
}

long tux_connect(int fd, const void *a, socklen_t al)
{
	struct sockaddr_storage d;
	socklen_t dl;
	int r = addr_l2d(a, al, &d, &dl, 1);
	if (r < 0) return r;
	return connect(fd, (struct sockaddr *)&d, dl) ? err() : 0;
}

long tux_listen(int fd, int backlog) { return listen(fd, backlog) ? err() : 0; }

static void out_addr(const struct sockaddr_storage *d, socklen_t dl, void *a, uint32_t *alp)
{
	if (!a || !alp) return;
	*alp = addr_d2l((const struct sockaddr *)d, dl, a, *alp);
}

long tux_accept(int fd, void *a, uint32_t *alp, long flags)
{
	struct sockaddr_storage d;
	socklen_t dl = sizeof d;
	int n = accept(fd, (struct sockaddr *)&d, &dl);
	if (n < 0) return err();
	if (flags & L_O_NONBLOCK) fcntl(n, F_SETFL, O_NONBLOCK);
	if (flags & L_SOCK_CLOEXEC) fcntl(n, F_SETFD, FD_CLOEXEC);
	if (tux_fdx_kind(fd) == K_SEQPKT) tux_fdx_mark_seqpkt(n);
	out_addr(&d, dl, a, alp);
	return n;
}

long tux_getsockname(int fd, void *a, uint32_t *alp)
{
	struct sockaddr_storage d;
	socklen_t dl = sizeof d;
	if (getsockname(fd, (struct sockaddr *)&d, &dl)) return err();
	out_addr(&d, dl, a, alp);
	return 0;
}
long tux_getpeername(int fd, void *a, uint32_t *alp)
{
	struct sockaddr_storage d;
	socklen_t dl = sizeof d;
	if (getpeername(fd, (struct sockaddr *)&d, &dl)) return err();
	out_addr(&d, dl, a, alp);
	return 0;
}
long tux_shutdown(int fd, int how) { return shutdown(fd, how) ? err() : 0; }

// MSG_NOSIGNAL: Darwin kennt es nicht. SO_NOSIGPIPE wirkt bei AF_UNIX nicht zuverlässig; darum SIGPIPE für die Dauer
// des Aufrufs sperren und ein dabei entstandenes Signal abholen.
typedef struct { int on; sigset_t old; } nosig_t;
static void nosig_on(int flags, nosig_t *n)
{
	n->on = (flags & 0x4000) != 0;
	if (!n->on) return;
	sigset_t b;
	sigemptyset(&b);
	sigaddset(&b, SIGPIPE);
	pthread_sigmask(SIG_BLOCK, &b, &n->old);
}
static void nosig_off(nosig_t *n)
{
	if (!n->on) return;
	sigset_t b, pend;
	sigemptyset(&b);
	sigaddset(&b, SIGPIPE);
	if (!sigismember(&n->old, SIGPIPE) && sigpending(&pend) == 0 && sigismember(&pend, SIGPIPE)) {
		int sig;
		sigwait(&b, &sig);
	}
	pthread_sigmask(SIG_SETMASK, &n->old, NULL);
}

long tux_sendto(int fd, const void *buf, size_t n, long flags, const void *a, socklen_t al)
{
	if (tux_fdx_kind(fd) == K_SEQPKT) { struct iovec v = {(void *)buf, n}; return seq_send(fd, &v, 1, NULL, 0); }
	struct sockaddr_storage d;
	socklen_t dl = 0;
	if (a && al) { int r = addr_l2d(a, al, &d, &dl, 1); if (r < 0) return r; }
	nosig_t ns;
	nosig_on((int)flags, &ns);
	ssize_t r = sendto(fd, buf, n, mflags_l2d((int)flags), dl ? (struct sockaddr *)&d : NULL, dl);
	long ret = r < 0 ? err() : r;
	nosig_off(&ns);
	return ret;
}

long tux_recvfrom(int fd, void *buf, size_t n, long flags, void *a, uint32_t *alp)
{
	if (tux_fdx_kind(fd) == K_SEQPKT) {
		struct iovec v = {buf, n};
		int tr;
		return seq_recv(fd, &v, 1, NULL, NULL, &tr);
	}
	struct sockaddr_storage d;
	socklen_t dl = sizeof d;
	ssize_t r = recvfrom(fd, buf, n, mflags_l2d((int)flags), (struct sockaddr *)&d, &dl);
	if (r < 0) return err();
	if (a && alp) { if (dl) out_addr(&d, dl, a, alp); else *alp = 0; }
	return r;
}

long tux_sendmsg(int fd, void *lm, long flags)
{
	struct l_msghdr *l = lm;
	socklen_t cl;
	void *ctl = ctl_l2d(l->control, l->controllen, &cl);
	long ret;
	if (tux_fdx_kind(fd) == K_SEQPKT) {
		ret = seq_send(fd, l->iov, (int)l->iovlen, ctl, cl);
		free(ctl);
		return ret;
	}
	struct sockaddr_storage d;
	struct msghdr m;
	memset(&m, 0, sizeof m);
	if (l->name && l->namelen) {
		socklen_t dl;
		int r = addr_l2d(l->name, l->namelen, &d, &dl, 1);
		if (r < 0) { free(ctl); return r; }
		m.msg_name = &d;
		m.msg_namelen = dl;
	}
	m.msg_iov = l->iov;
	m.msg_iovlen = (int)l->iovlen;
	m.msg_control = ctl;
	m.msg_controllen = cl;
	nosig_t ns;
	nosig_on((int)flags, &ns);
	ssize_t r = sendmsg(fd, &m, mflags_l2d((int)flags));
	ret = r < 0 ? err() : r;
	nosig_off(&ns);
	free(ctl);
	return ret;
}

long tux_recvmsg(int fd, void *lm, long flags)
{
	struct l_msghdr *l = lm;
	uint64_t lcap = l->control ? l->controllen : 0;
	if (lcap > 4096) lcap = 4096;
	uint8_t dctl[4096];
	socklen_t dcl = (socklen_t)lcap;
	int trunc = 0, ctrunc = 0;
	long ret;
	int fl = 0;
	if (tux_fdx_kind(fd) == K_SEQPKT) {
		ret = seq_recv(fd, l->iov, (int)l->iovlen, lcap ? dctl : NULL, &dcl, &trunc);
		if (trunc) fl |= 0x20;
		l->namelen = 0;
	} else {
		struct sockaddr_storage d;
		struct msghdr m;
		memset(&m, 0, sizeof m);
		m.msg_name = &d;
		m.msg_namelen = sizeof d;
		m.msg_iov = l->iov;
		m.msg_iovlen = (int)l->iovlen;
		m.msg_control = lcap ? dctl : NULL;
		m.msg_controllen = (socklen_t)lcap;
		ssize_t r = recvmsg(fd, &m, mflags_l2d((int)flags));
		if (r < 0) return err();
		ret = r;
		dcl = m.msg_controllen;
		fl = mflags_d2l(m.msg_flags);
		if (l->name && l->namelen) {
			if (m.msg_namelen) l->namelen = addr_d2l((struct sockaddr *)&d, m.msg_namelen, l->name, l->namelen);
			else l->namelen = 0;
		} else l->namelen = 0;
	}
	if (ret < 0) return ret;
	if (lcap) {
		uint64_t used = ctl_d2l(dctl, dcl, l->control, lcap, (flags & 0x40000000) != 0, &ctrunc);
		l->controllen = used;
		if (ctrunc) fl |= 8;
	} else l->controllen = 0;
	l->flags = fl;
	return ret;
}

struct l_mmsghdr { struct l_msghdr hdr; uint32_t len; int pad; };

long tux_sendmmsg(int fd, void *vec, unsigned vlen, long flags)
{
	struct l_mmsghdr *v = vec;
	unsigned i;
	for (i = 0; i < vlen; i++) {
		long r = tux_sendmsg(fd, &v[i].hdr, flags);
		if (r < 0) return i ? (long)i : r;
		v[i].len = (uint32_t)r;
	}
	return i;
}

long tux_recvmmsg(int fd, void *vec, unsigned vlen, long flags)
{
	struct l_mmsghdr *v = vec;
	unsigned i;
	for (i = 0; i < vlen; i++) {
		long r = tux_recvmsg(fd, &v[i].hdr, i ? flags | 0x40 : flags);
		if (r < 0) return i ? (long)i : r;
		v[i].len = (uint32_t)r;
	}
	return i;
}
