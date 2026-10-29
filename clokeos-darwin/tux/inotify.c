// tux – inotify über kqueue (EVFILT_VNODE). Der inotify-Deskriptor ist ein kqueue (damit epoll/poll ihn sehen); jede
// Überwachung hält die Datei offen. Änderungen in Ordnern meldet Darwin nur als "Ordner geändert": tux vergleicht dann
// die Namensliste mit dem Stand davor und macht daraus IN_CREATE und IN_DELETE (ein Umbenennen erscheint als Löschen
// plus Anlegen, ohne cookie).
#include "tux.h"
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/event.h>
#include <sys/stat.h>
#include <unistd.h>

#define IN_ACCESS 0x1
#define IN_MODIFY 0x2
#define IN_ATTRIB 0x4
#define IN_CLOSE_WRITE 0x8
#define IN_MOVED_FROM 0x40
#define IN_MOVED_TO 0x80
#define IN_CREATE 0x100
#define IN_DELETE 0x200
#define IN_DELETE_SELF 0x400
#define IN_MOVE_SELF 0x800
#define IN_IGNORED 0x8000
#define IN_ISDIR 0x40000000
#define IN_ONLYDIR 0x01000000
#define IN_DONT_FOLLOW 0x02000000
#define IN_MASK_ADD 0x20000000
#define IN_ONESHOT 0x80000000u
#define L_O_NONBLOCK 04000
#define L_CLOEXEC 02000000
#define MAXFD 4096

typedef struct watch {
	int wd, fd;
	uint32_t mask;
	int isdir;
	char **names; int nnames;
} watch;

typedef struct ino {
	int refs;
	watch *w; int nw, capw;
	int next_wd;
	uint8_t *q; size_t qlen, qcap;   // erzeugte, noch nicht gelesene Ereignisse
} ino;

static ino *tab[MAXFD];
static pthread_mutex_t lk = PTHREAD_MUTEX_INITIALIZER;

static void push(ino *o, int wd, uint32_t mask, const char *name)
{
	size_t nl = name ? strlen(name) + 1 : 0, pad = (nl + 7) & ~7UL, need = 16 + pad;
	if (o->qlen + need > o->qcap) { o->qcap = (o->qlen + need) * 2; o->q = realloc(o->q, o->qcap); }
	uint8_t *p = o->q + o->qlen;
	uint32_t hdr[4] = {(uint32_t)wd, mask, 0, (uint32_t)pad};
	memcpy(p, hdr, 16);
	memset(p + 16, 0, pad);
	if (nl) memcpy(p + 16, name, nl);
	o->qlen += need;
}

static void free_names(watch *w)
{
	for (int i = 0; i < w->nnames; i++) free(w->names[i]);
	free(w->names);
	w->names = NULL;
	w->nnames = 0;
}

static void scan_names(watch *w, char ***out, int *n)
{
	*out = NULL;
	*n = 0;
	char path[PATH_MAX];
	if (fcntl(w->fd, F_GETPATH, path) < 0) return;
	DIR *d = opendir(path);
	if (!d) return;
	struct dirent *e;
	int cap = 0;
	while ((e = readdir(d))) {
		if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
		if (*n == cap) { cap = cap ? cap * 2 : 32; *out = realloc(*out, cap * sizeof(char *)); }
		(*out)[(*n)++] = strdup(e->d_name);
	}
	closedir(d);
}

static int has(char **v, int n, const char *s) { for (int i = 0; i < n; i++) if (!strcmp(v[i], s)) return 1; return 0; }

long tux_inotify_init(long flags)
{
	int kq = kqueue();
	if (kq < 0) return -tux_errno(errno);
	if (kq >= MAXFD) { close(kq); return -tux_errno(EMFILE); }
	if (flags & L_O_NONBLOCK) fcntl(kq, F_SETFL, O_NONBLOCK);
	if (flags & L_CLOEXEC) fcntl(kq, F_SETFD, FD_CLOEXEC);
	ino *o = calloc(1, sizeof *o);
	o->refs = 1;
	o->next_wd = 1;
	pthread_mutex_lock(&lk);
	tab[kq] = o;
	pthread_mutex_unlock(&lk);
	return kq;
}

long tux_inotify_add(int fd, const char *guest_path, uint32_t mask)
{
	ino *o = fd >= 0 && fd < MAXFD ? tab[fd] : NULL;
	if (!o) return -tux_errno(EINVAL);
	char host[PATH_MAX];
	int r = tux_resolve(-100, guest_path, !(mask & IN_DONT_FOLLOW), host, NULL);
	if (r < 0) return r;
	int wf = open(host, O_RDONLY | O_EVTONLY);
	if (wf < 0) return -tux_errno(errno);
	struct stat st;
	fstat(wf, &st);
	int isdir = S_ISDIR(st.st_mode);
	if ((mask & IN_ONLYDIR) && !isdir) { close(wf); return -tux_errno(ENOTDIR); }
	pthread_mutex_lock(&lk);
	// derselbe Ort (gleiche Datei) wie eine vorhandene Überwachung: deren Maske ersetzen oder ergänzen
	for (int i = 0; i < o->nw; i++) {
		struct stat s2;
		if (fstat(o->w[i].fd, &s2) == 0 && s2.st_ino == st.st_ino && s2.st_dev == st.st_dev) {
			o->w[i].mask = (mask & IN_MASK_ADD) ? (o->w[i].mask | mask) : mask;
			int wd = o->w[i].wd;
			pthread_mutex_unlock(&lk);
			close(wf);
			return wd;
		}
	}
	if (o->nw == o->capw) { o->capw = o->capw ? o->capw * 2 : 8; o->w = realloc(o->w, o->capw * sizeof(watch)); }
	watch *w = &o->w[o->nw++];
	memset(w, 0, sizeof *w);
	w->wd = o->next_wd++;
	w->fd = wf;
	w->mask = mask;
	w->isdir = isdir;
	if (isdir) scan_names(w, &w->names, &w->nnames);
	struct kevent k;
	EV_SET(&k, wf, EVFILT_VNODE, EV_ADD | EV_CLEAR | EV_ENABLE,
	    NOTE_WRITE | NOTE_EXTEND | NOTE_ATTRIB | NOTE_DELETE | NOTE_RENAME | NOTE_LINK, 0, (void *)(intptr_t)w->wd);
	kevent(fd, &k, 1, NULL, 0, NULL);
	int wd = w->wd;
	pthread_mutex_unlock(&lk);
	return wd;
}

long tux_inotify_rm(int fd, int wd)
{
	ino *o = fd >= 0 && fd < MAXFD ? tab[fd] : NULL;
	if (!o) return -tux_errno(EINVAL);
	long ret = -tux_errno(EINVAL);
	pthread_mutex_lock(&lk);
	for (int i = 0; i < o->nw; i++)
		if (o->w[i].wd == wd) {
			close(o->w[i].fd);   // der kqueue verliert den Eintrag mit dem Schließen
			free_names(&o->w[i]);
			push(o, wd, IN_IGNORED, NULL);
			o->w[i] = o->w[--o->nw];
			ret = 0;
			break;
		}
	pthread_mutex_unlock(&lk);
	return ret;
}

// Rückgabe 1, wenn fd ein inotify-Deskriptor ist
int tux_ino_read(int fd, void *buf, size_t n, long *ret)
{
	ino *o = fd >= 0 && fd < MAXFD ? tab[fd] : NULL;
	if (!o) return 0;
	for (;;) {
		pthread_mutex_lock(&lk);
		if (o->qlen) {
			size_t take = 0;
			while (take < o->qlen) {
				uint32_t len;
				memcpy(&len, o->q + take + 12, 4);
				if (take + 16 + len > n) break;
				take += 16 + len;
			}
			if (!take) { pthread_mutex_unlock(&lk); *ret = -tux_errno(EINVAL); return 1; }
			memcpy(buf, o->q, take);
			memmove(o->q, o->q + take, o->qlen - take);
			o->qlen -= take;
			pthread_mutex_unlock(&lk);
			*ret = (long)take;
			return 1;
		}
		pthread_mutex_unlock(&lk);
		struct kevent kev[16];
		struct timespec zero = {0, 0};
		int fl = fcntl(fd, F_GETFL);
		int nb = fl >= 0 && (fl & O_NONBLOCK);
		int k = kevent(fd, NULL, 0, kev, 16, nb ? &zero : NULL);
		if (k < 0) { *ret = -tux_errno(errno); return 1; }
		if (k == 0) { *ret = -tux_errno(EAGAIN); return 1; }
		pthread_mutex_lock(&lk);
		for (int i = 0; i < k; i++) {
			int wd = (int)(intptr_t)kev[i].udata;
			watch *w = NULL;
			for (int j = 0; j < o->nw; j++) if (o->w[j].wd == wd) w = &o->w[j];
			if (!w) continue;
			uint32_t f = kev[i].fflags, m = w->mask, dirflag = w->isdir ? IN_ISDIR : 0;
			if ((f & (NOTE_WRITE | NOTE_EXTEND)) && w->isdir) {
				char **now;
				int nn;
				scan_names(w, &now, &nn);
				for (int a = 0; a < nn; a++)
					if (!has(w->names, w->nnames, now[a]) && (m & IN_CREATE)) push(o, w->wd, IN_CREATE, now[a]);
				for (int a = 0; a < w->nnames; a++)
					if (!has(now, nn, w->names[a]) && (m & IN_DELETE)) push(o, w->wd, IN_DELETE, w->names[a]);
				free_names(w);
				w->names = now;
				w->nnames = nn;
			} else if ((f & (NOTE_WRITE | NOTE_EXTEND)) && (m & IN_MODIFY)) push(o, w->wd, IN_MODIFY, NULL);
			if ((f & NOTE_ATTRIB) && (m & IN_ATTRIB)) push(o, w->wd, IN_ATTRIB | dirflag, NULL);
			if ((f & NOTE_RENAME) && (m & IN_MOVE_SELF)) push(o, w->wd, IN_MOVE_SELF | dirflag, NULL);
			if (f & NOTE_DELETE) {
				if (m & IN_DELETE_SELF) push(o, w->wd, IN_DELETE_SELF | dirflag, NULL);
				push(o, w->wd, IN_IGNORED, NULL);
			}
			if (m & IN_ONESHOT) {
				close(w->fd);
				free_names(w);
				*w = o->w[--o->nw];
			}
		}
		pthread_mutex_unlock(&lk);
		// nichts Brauchbares daraus entstanden: noch einmal warten (blockierend) oder EAGAIN
		pthread_mutex_lock(&lk);
		int empty = o->qlen == 0;
		pthread_mutex_unlock(&lk);
		if (empty && nb) { *ret = -tux_errno(EAGAIN); return 1; }
	}
}

void tux_ino_dup(int oldfd, int newfd)
{
	if (oldfd < 0 || oldfd >= MAXFD || newfd < 0 || newfd >= MAXFD || !tab[oldfd]) return;
	pthread_mutex_lock(&lk);
	tab[newfd] = tab[oldfd];
	tab[newfd]->refs++;
	pthread_mutex_unlock(&lk);
}

void tux_ino_close(int fd)
{
	if (fd < 0 || fd >= MAXFD) return;
	pthread_mutex_lock(&lk);
	ino *o = tab[fd];
	tab[fd] = NULL;
	if (o && --o->refs <= 0) {
		for (int i = 0; i < o->nw; i++) { close(o->w[i].fd); free_names(&o->w[i]); }
		free(o->w);
		free(o->q);
		free(o);
	}
	pthread_mutex_unlock(&lk);
}
