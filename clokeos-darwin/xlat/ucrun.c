// ucrun – x86_64-Mach-O auf arm64 ausführen, mit Unicorn (QEMU-TCG als Bibliothek) als CPU-Kern.
//
// Gastspeicher liegt in Unicorns eigenem Adressraum (4-KiB-Seiten, jede Gastadresse frei): Pagezero, PIE-Verschiebung
// und die 16-KiB-Seite des Hosts spielen keine Rolle. Die Systemaufrufe des Gastes (BSD-Klasse 2; Nummern und
// LP64-Strukturen sind auf Darwin für x86_64 und arm64 gleich) werden mit einer kleinen Tabelle (Zeiger rein/raus)
// an den Host weitergereicht, Fehler wie bei x86_64-Darwin (Carry-Flag + errno in rax).
//
//   ucrun <x86_64-Mach-O> [Argumente]     (UCRUN_TRACE=1: Systemaufrufe zeigen, UCRUN_STATS=1: Zeit/Zähler am Ende)
#include <errno.h>
#include <fcntl.h>
#include <mach-o/loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <unistd.h>
#include <unicorn/unicorn.h>

#define STACK_TOP 0x7ffee0100000ULL
#define STACK_SIZE 0x800000ULL
#define MMAP_BASE 0x600000000000ULL
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

extern char **environ;
static uc_engine *uc;
static int trace;
static unsigned long nsys;
static uint64_t mmap_next = MMAP_BASE;
static double t0;

static const char *xroot = "";
#define DYLD_BASE 0x7fff20000000ULL

// Lädt ein x86_64-Mach-O (dünn) in den Gastspeicher; slide wird auf alle Adressen addiert.
static int load_image(const uint8_t *file, uint64_t slide, uint64_t *entry, uint64_t *mh_addr, int *dynamic) {
	struct mach_header_64 *mh = (void *)file; uc_err e;
	struct load_command *lc = (void *)(mh + 1);
	for (uint32_t i = 0; i < mh->ncmds; i++, lc = (void *)((char *)lc + lc->cmdsize)) {
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64 *s = (void *)lc;
			if (!strcmp(s->segname, "__PAGEZERO") || !s->vmsize) continue;
			uint64_t va = s->vmaddr + slide;
			uint64_t base = va & ~0xfffULL, sz = (va + s->vmsize - base + 0xfff) & ~0xfffULL;
			uc_mem_unmap(uc, base, sz);
			if ((e = uc_mem_map(uc, base, sz, UC_PROT_ALL)) && e != UC_ERR_MAP) { fprintf(stderr, "map %s: %s\n", s->segname, uc_strerror(e)); return -1; }
			uc_mem_write(uc, va, file + s->fileoff, s->filesize);
			if (!strcmp(s->segname, "__TEXT")) *mh_addr = va;
		} else if (lc->cmd == LC_UNIXTHREAD) {
			*entry = *(uint64_t *)((uint32_t *)((char *)lc + 8) + 2 + 16 * 2) + slide;
		} else if (lc->cmd == LC_LOAD_DYLINKER || lc->cmd == LC_MAIN) {
			*dynamic = 1;
		}
	}
	return 0;
}

static bool on_unmapped(uc_engine *u, uc_mem_type t, uint64_t addr, int size, int64_t val, void *ud) {
	fprintf(stderr, "ucrun: Zugriff auf nicht abgebildeten Gastspeicher: Art %d, Adresse %#llx, Größe %d\n", (int)t, (unsigned long long)addr, size);
	return false;
}
// Antwort auf eine MIG-Nachricht an den Host-Port in den Empfangspuffer schreiben
static void mig_reply(uc_engine *u, uint64_t addr, uint32_t id, uint32_t retcode, const void *payload, uint32_t plen) {
	uint8_t r[512] = {0}; uint32_t bits = 0x12, size = 24 + 8 + 4 + plen, ndr[2] = {0, 1};
	memcpy(r, &bits, 4); memcpy(r + 4, &size, 4); memcpy(r + 20, &id, 4); memcpy(r + 24, ndr, 8); memcpy(r + 32, &retcode, 4);
	if (plen) memcpy(r + 36, payload, plen);
	uc_mem_write(u, addr, r, size);
}
static void mig_reply_port(uc_engine *u, uint64_t addr, uint32_t id, uint32_t port) {
	uint8_t r[40] = {0}; uint32_t bits = 0x80000012, size = 40, cnt = 1;
	memcpy(r, &bits, 4); memcpy(r + 4, &size, 4); memcpy(r + 20, &id, 4); memcpy(r + 24, &cnt, 4); memcpy(r + 28, &port, 4);
	r[38] = 17; r[39] = 0;  // disposition MOVE_SEND, type PORT_DESCRIPTOR
	uc_mem_write(u, addr, r, 40);
}
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec / 1e6; }

static int gstr(uint64_t a, char *b, size_t n) {  // C-String aus dem Gast
	for (size_t i = 0; i < n; i++) { if (uc_mem_read(uc, a + i, b + i, 1)) return -1; if (!b[i]) return 0; }
	return -1;
}

// Gastzeiger-Beschreibung eines Aufrufs: arg index, Richtung ('i' rein, 'o' raus, 's' Pfad), Größe fest oder aus Argument
typedef struct { int nr; const char *name; struct { int idx; char dir; int fixed, size_arg; } p[3]; } Sc;
#define NONE {-1, 0, 0, 0}
static const Sc tab[] = {
	{3, "read",  {{1, 'o', 0, 2}, NONE}},      {4, "write", {{1, 'i', 0, 2}, NONE}},
	{5, "open",  {{0, 's', 0, 0}, NONE}},      {6, "close", {NONE, NONE}},
	{20, "getpid", {NONE, NONE}},              {24, "getuid", {NONE, NONE}},
	{43, "getegid", {NONE, NONE}},             {47, "getgid", {NONE, NONE}},
	{25, "geteuid", {NONE, NONE}},
	{33, "access", {{0, 's', 0, 0}, NONE}},    {41, "dup", {NONE, NONE}},
	{10, "unlink", {{0, 's', 0, 0}, NONE}},    {136, "mkdir", {{0, 's', 0, 0}, NONE}},
	{137, "rmdir", {{0, 's', 0, 0}, NONE}},    {199, "lseek", {NONE, NONE}},
	{92, "fcntl", {NONE, NONE}},               {116, "gettimeofday", {{0, 'o', 16, 0}, {1, 'o', 8, 0}}},
	{153, "pread", {{1, 'o', 0, 2}, NONE}},    {154, "pwrite", {{1, 'i', 0, 2}, NONE}},
	{338, "stat64", {{0, 's', 0, 0}, {1, 'o', 144, 0}}}, {340, "lstat64", {{0, 's', 0, 0}, {1, 'o', 144, 0}}},
	{339, "fstat64", {{1, 'o', 144, 0}, NONE}},
	{500, "getentropy", {{0, 'o', 0, 1}, NONE}},
	{327, "issetugid", {NONE, NONE}},
	{194, "getrlimit", {{1, 'o', 16, 0}, NONE}},
	{220, "getattrlist", {{0, 's', 0, 0}, {1, 'i', 24, 0}, {2, 'o', 0, 3}}},
};

static uint64_t gmap(uint64_t addr, uint64_t len, int fixed) {
	len = (len + 0xfff) & ~0xfffULL;
	if (!fixed || !addr) { addr = mmap_next; mmap_next += len + 0x1000; }
	else uc_mem_unmap(uc, addr, len);  // MAP_FIXED überschreibt
	if (uc_mem_map(uc, addr, len, UC_PROT_ALL)) return 0;
	return addr;
}

static void do_syscall(uc_engine *u, void *ud) {
	uint64_t rax, a[6], rdx = 0; int64_t res; int err = 0; nsys++;
	uc_reg_read(u, UC_X86_REG_RAX, &rax);
	uc_reg_read(u, UC_X86_REG_RDI, &a[0]); uc_reg_read(u, UC_X86_REG_RSI, &a[1]); uc_reg_read(u, UC_X86_REG_RDX, &a[2]);
	uc_reg_read(u, UC_X86_REG_R10, &a[3]); uc_reg_read(u, UC_X86_REG_R8, &a[4]); uc_reg_read(u, UC_X86_REG_R9, &a[5]);
	if ((rax >> 24) == 1) {  // Mach-Fallen: nur was dyld/libSystem beim Start brauchen, mit festen Port-Namen
		uint64_t tn = rax & 0xffffff;
		if (trace) fprintf(stderr, "ucrun: mach trap %llu(%#llx, %#llx, %#llx)\n", (unsigned long long)tn, (unsigned long long)a[0], (unsigned long long)a[1], (unsigned long long)a[2]);
		res = 0;
		switch (tn) {
		case 26: res = 0x303; break;   // mach_reply_port
		case 27: res = 0x203; break;   // thread_self_trap
		case 28: res = 0x103; break;   // task_self_trap
		case 29: res = 0x403; break;   // host_self_trap
		case 10: {                     // _kernelrpc_mach_vm_allocate_trap(task, *addr, size, flags)
			uint64_t want = 0; uc_mem_read(u, a[1], &want, 8);
			uint64_t g = gmap((a[3] & 1) ? 0 : want, a[2], !(a[3] & 1) && want);
			if (!g) { res = 3; break; }
			uc_mem_write(u, a[1], &g, 8); break; }
		case 89: { uint32_t tb[2] = {1, 1}; uc_mem_write(u, a[0], tb, 8); break; }  // mach_timebase_info_trap
		case 15: {                     // _kernelrpc_mach_vm_map_trap(task, *addr, size, mask, flags, prot)
			uint64_t al = a[3] + 1; if (al > 1) mmap_next = (mmap_next + al - 1) & ~(al - 1);
			uint64_t g = gmap(0, a[2], 0);
			if (!g) { res = 3; break; }
			uc_mem_write(u, a[1], &g, 8); break; }
		case 16: { uint32_t nm = 0x1003; uc_mem_write(u, a[2], &nm, 4); break; }  // mach_port_allocate_trap(task, right, *name)
		case 12: uc_mem_unmap(u, a[1], (a[2] + 0xfff) & ~0xfffULL); break;  // mach_vm_deallocate
		case 14: break;                // mach_vm_protect: Gastspeicher bleibt RWX
		case 18: case 19: break;       // mach_port_deallocate / mod_refs
		case 31: {  // mach_msg_trap: nur Nachrichten an den Host-Port, als kleine MIG-Server
			uint8_t m[0x40]; uc_mem_read(u, a[0], m, 0x40); uint32_t id = *(uint32_t *)(m + 20);
			if (trace) fprintf(stderr, "ucrun: mach_msg id %u an Port %#x flavor %u count %u\n", id, *(uint32_t *)(m + 8), *(uint32_t *)(m + 32), *(uint32_t *)(m + 36));
			if (id == 200) {  // host_info(flavor, count)
				uint32_t flavor = *(uint32_t *)(m + 32);
				if (flavor == 1) {  // HOST_BASIC_INFO
					uint32_t p[1 + 12] = {12, 1, 1, 4096, 0x01000007, 3, 0, 1, 1, 1, 1, 0, 1};  // count, max/avail cpus, memory_size, type, subtype, ..., max_mem (64 bit)
					p[11] = 0; p[12] = 1;
					mig_reply(u, a[0], 300, 0, p, sizeof p);
				} else if (flavor == 5) {  // HOST_PRIORITY_INFO
					uint32_t p[1 + 8] = {8, 81, 80, 80, 31, 0, 0, 0, 127};
					mig_reply(u, a[0], 300, 0, p, sizeof p);
				} else mig_reply(u, a[0], 300, 4, 0, 0);
			} else if (id == 3409) { mig_reply_port(u, a[0], 3509, 0);   // task_get_special_port: kein Bootstrap-Server -> MACH_PORT_NULL
			} else if (id == 3418) { mig_reply_port(u, a[0], 3518, 0x1005); // semaphore_create
			} else if (id == 206) { mig_reply_port(u, a[0], 306, 0x1004); // host_get_clock_service
			} else { fprintf(stderr, "ucrun: mach_msg id %u nicht umgesetzt\n", id); mig_reply(u, a[0], id + 100, 5, 0, 0); }
			break; }
		default: fprintf(stderr, "ucrun: Mach-Falle %llu nicht umgesetzt\n", (unsigned long long)tn); res = 5;
		}
		goto mach_done;
	}
	if ((rax >> 24) == 3) {  // machdep: thread_set_tsd_base(3) = %gs-Basis
		if ((rax & 0xffffff) == 3) { uc_reg_write(u, UC_X86_REG_GS_BASE, &a[0]); res = 0; goto mach_done; }
	}
	if ((rax >> 24) != 2) { fprintf(stderr, "ucrun: Systemaufruf-Klasse %llu (Mach-Falle?) nicht umgesetzt: %#llx\n", (unsigned long long)(rax >> 24), (unsigned long long)rax); err = ENOSYS; res = ENOSYS; goto done; }
	uint64_t nr = rax & 0xffffff;
	if (trace) fprintf(stderr, "ucrun: sys %llu(%#llx, %#llx, %#llx)\n", (unsigned long long)nr, (unsigned long long)a[0], (unsigned long long)a[1], (unsigned long long)a[2]);
	if (nr == 1) {
		if (getenv("UCRUN_STATS")) fprintf(stderr, "ucrun: %.3f s, %lu Systemaufrufe\n", now() - t0, nsys);
		exit((int)a[0]);
	}
	if (nr == 197) {  // mmap(addr, len, prot, flags, fd, off): anonym oder Datei (Inhalt wird kopiert, kein Teilen)
		uint64_t g = gmap(a[0], a[1], (a[3] & MAP_FIXED) != 0);
		if (!g) { err = ENOMEM; res = ENOMEM; goto done; }
		if (!(a[3] & MAP_ANON)) {
			char *b = malloc(a[1]); ssize_t n = pread((int)a[4], b, a[1], (off_t)a[5]);
			if (n > 0) uc_mem_write(u, g, b, n);
			free(b);
		}
		res = g; goto done;
	}
	switch (nr) {  // Systemaufrufe, die nur beim Start nach dem Zustand des Systems fragen
	case 483: case 366: case 381: case 520: case 521: case 48: case 46: case 329: res = 0; goto done;                 // csrctl, bsdthread_register, __mac_syscall
	case 54: err = 25; res = 25; goto done;                              // ioctl: ENOTTY (Terminal-Abfragen der libc)
	case 274: err = 2; res = 2; goto done;                               // __sysctlbyname: ENOENT
	case 294: case 336: case 287: err = EINVAL; res = EINVAL; goto done; // shared_region_check_np (kein Cache), proc_info, ...
	}
	if (nr == 92) {  // fcntl: Code-Signatur-Befehle gibt es hier nicht (PureDarwin erzwingt keine), F_GETPATH braucht einen Puffer
		int cmd = (int)a[1];
		if (cmd == 98) { res = 0; goto done; }                                  // F_CHECK_LV
		if (cmd == 59 || cmd == 61 || cmd == 97 || cmd == 99) { err = 45; res = 45; goto done; }  // F_ADDFILESIGS*, F_ADDSIGS: ENOTSUP
		if (cmd == 50) { char p[1024] = {0}; res = fcntl((int)a[0], 50, p); if (res == -1) { err = errno; res = errno; } else { uc_mem_write(u, a[2], p, 1024); } goto done; }
		res = fcntl((int)a[0], cmd, a[2]); if (res == -1) { err = errno; res = errno; } goto done;
	}
	if (nr == 202) {  // sysctl(name, namelen, oldp, oldlenp, newp, newlen): Namen und Werte gelten für den Host (arm64) - siehe Studie
		int mib[16]; uint32_t nl = a[1] > 16 ? 16 : a[1]; uc_mem_read(u, a[0], mib, nl * 4);
		size_t ol = 0; if (a[3]) uc_mem_read(u, a[3], &ol, 8);
		char *ob = a[2] ? calloc(1, ol ? ol : 1) : 0; void *nb = 0;
		if (a[4]) { nb = malloc(a[5]); uc_mem_read(u, a[4], nb, a[5]); }
		res = sysctl(mib, nl, ob, a[3] ? &ol : 0, nb, a[5]);
		if (res == -1) { err = errno; res = errno; }
		if (ob && (res == 0 || err == ENOMEM)) uc_mem_write(u, a[2], ob, ol);
		if (a[3] && (res == 0 || err == ENOMEM)) uc_mem_write(u, a[3], &ol, 8);
		free(ob); free(nb); goto done;
	}
	if (nr == 372) { res = 1; goto done; }  // thread_selfid
	if (nr == 73) { uc_mem_unmap(u, a[0], (a[1] + 0xfff) & ~0xfffULL); res = 0; goto done; }
	if (nr == 74) { res = 0; goto done; }  // mprotect: Gastspeicher bleibt RWX (Eigenschaft von ucrun, siehe Studie)
	const Sc *s = 0;
	for (unsigned i = 0; i < sizeof tab / sizeof *tab; i++) if (tab[i].nr == (int)nr) s = &tab[i];
	if (!s) { fprintf(stderr, "ucrun: Systemaufruf %llu nicht umgesetzt\n", (unsigned long long)nr); err = ENOSYS; res = ENOSYS; goto done; }
	uint64_t h[6]; memcpy(h, a, sizeof h);
	char *bufs[3] = {0, 0, 0}; size_t sz[3] = {0, 0, 0}; char pathb[3][1024];
	for (int k = 0; k < 3; k++) {
		int i = s->p[k].idx; if (i < 0 || !s->p[k].dir) continue;
		if (s->p[k].dir == 's') { if (gstr(a[i], pathb[k], 1024)) { err = EFAULT; res = EFAULT; goto done; } if (*xroot && pathb[k][0] == '/') {  // Gast-Dateisystem: erst XLAT_ROOT (x86_64-Systembibliotheken), sonst der Host
				char alt[1100]; snprintf(alt, sizeof alt, "%s%s", xroot, pathb[k]);
				if (access(alt, F_OK) == 0) strncpy(pathb[k], alt, 1023);
			}
			h[i] = (uint64_t)pathb[k]; continue; }
		sz[k] = s->p[k].fixed ? (size_t)s->p[k].fixed : (size_t)a[s->p[k].size_arg];
		bufs[k] = calloc(1, sz[k] ? sz[k] : 1);
		if (s->p[k].dir == 'i') uc_mem_read(u, a[i], bufs[k], sz[k]);
		h[i] = (uint64_t)bufs[k];
	}
	if (nr == 339 && 0) {}
	if (nr == 338) res = stat((char *)h[0], (struct stat *)h[1]);        // libc statt SYS_stat64: auf arm64-Hosts nicht überall vorhanden
	else if (nr == 340) res = lstat((char *)h[0], (struct stat *)h[1]);
	else if (nr == 339) res = fstat((int)h[0], (struct stat *)h[1]);
	else res = syscall((int)nr, h[0], h[1], h[2], h[3], h[4], h[5]);
	if (res == -1) { err = errno; res = errno; }
	else for (int k = 0; k < 3; k++) if (bufs[k] && s->p[k].dir == 'o') {
		size_t n = sz[k]; if (nr == 3 || nr == 153) n = res < 0 ? 0 : (size_t)res;
		uc_mem_write(u, a[s->p[k].idx], bufs[k], n);
	}
	for (int k = 0; k < 3; k++) free(bufs[k]);
	goto done;
mach_done:;
	uc_reg_write(u, UC_X86_REG_RAX, &res);
	return;
done:;
	uint64_t fl; uc_reg_read(u, UC_X86_REG_EFLAGS, &fl);
	fl = err ? (fl | 1) : (fl & ~1ULL);
	uc_reg_write(u, UC_X86_REG_EFLAGS, &fl);
	uc_reg_write(u, UC_X86_REG_RAX, &res);
	uc_reg_write(u, UC_X86_REG_RDX, &rdx);
}


int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: ucrun <x86_64 Mach-O> [args]\n"); return 2; }
	trace = getenv("UCRUN_TRACE") != 0;
	if (getenv("XLAT_ROOT")) xroot = getenv("XLAT_ROOT");
	FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 127; }
	fseek(f, 0, SEEK_END); long fl = ftell(f); fseek(f, 0, SEEK_SET);
	uint8_t *file = malloc(fl); if (fread(file, 1, fl, f) != (size_t)fl) return 2;
	if (*(uint32_t *)file == 0xbebafeca) {  // fat: x86_64-Scheibe suchen (Kopf big endian)
		uint32_t na = __builtin_bswap32(*(uint32_t *)(file + 4)), off = 0;
		for (uint32_t i = 0; i < na; i++) if (__builtin_bswap32(*(uint32_t *)(file + 8 + 20 * i)) == 0x01000007) off = __builtin_bswap32(*(uint32_t *)(file + 16 + 20 * i));
		if (off) file += off;
	}
	struct mach_header_64 *mh = (void *)file;
	if (mh->magic != MH_MAGIC_64 || mh->cputype != 0x01000007) { fprintf(stderr, "ucrun: %s ist kein x86_64-Mach-O\n", argv[1]); return 126; }
	uc_err e = uc_open(UC_ARCH_X86, UC_MODE_64, &uc); if (e) { fprintf(stderr, "uc_open: %s\n", uc_strerror(e)); return 3; }
	uint64_t entry = 0, mh_addr = 0; int dynamic = 0;
	if (load_image(file, 0, &entry, &mh_addr, &dynamic)) return 3;
	uint64_t unix_entry = entry;
	if (dynamic) {  // wie der Kernel: dyld des Gastes laden, es lädt Programm und Bibliotheken selbst
		char dp[1024]; snprintf(dp, sizeof dp, "%s/usr/lib/dyld", xroot);
		FILE *df = fopen(dp, "rb"); if (!df) { fprintf(stderr, "ucrun: %s fehlt (XLAT_ROOT mit x86_64-Systembibliotheken)\n", dp); return 125; }
		fseek(df, 0, SEEK_END); long dl = ftell(df); fseek(df, 0, SEEK_SET);
		uint8_t *dfile = malloc(dl); if (fread(dfile, 1, dl, df) != (size_t)dl) return 2;
		uint64_t dmh = 0, dentry = 0; int dd = 0;
		if (load_image(dfile, DYLD_BASE, &dentry, &dmh, &dd)) return 3;
		entry = dentry;
	}
	// Commpage von x86_64-Darwin (Adresse im Host nicht abbildbar, in Unicorn schon): nur die Felder, die libSystem liest
	{
		uint8_t cp[0x4000] = {0}; uint64_t caps = 0x1fULL; uint16_t ver = 14; uint8_t ncpu = 1; uint64_t mem = 4ULL << 30; uint32_t fam = 0x1ee2d34e;
		memcpy(cp, "commpage 64-bit", 16); memcpy(cp + 0x10, &caps, 8); memcpy(cp + 0x1e, &ver, 2); cp[0x22] = ncpu;
		cp[0x34] = cp[0x35] = cp[0x36] = ncpu; memcpy(cp + 0x38, &mem, 8); memcpy(cp + 0x40, &fam, 4);
		{ uint32_t scale = 0xffffffffu, gen = 1; memcpy(cp + 0x58, &scale, 4); memcpy(cp + 0x68, &gen, 4); }  // nanotime: 1 ns je TSC-Takt
		uc_mem_map(uc, 0x7fffffe00000ULL, sizeof cp, UC_PROT_READ | UC_PROT_EXEC);
		uc_mem_write(uc, 0x7fffffe00000ULL, cp, sizeof cp);
	}
	uc_mem_map(uc, STACK_TOP - STACK_SIZE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
	// Stapel wie ihn der Kernel für ein Programm mit LC_UNIXTHREAD baut: argc, argv[], 0, envp[], 0, apple[], 0, Strings
	// Umgebung des Gastes: ohne DYLD_INSERT_LIBRARIES des Hosts (die arm64-Bibliothek passt nicht zum x86_64-dyld)
	static char *genv[1024]; int ne = 0;
	for (int i = 0; environ[i] && ne < 1000; i++) if (strncmp(environ[i], "DYLD_INSERT_LIBRARIES=", 22)) genv[ne++] = environ[i];
	uint64_t sp = STACK_TOP - 0x1000, ptrs[512]; int np = 0;
	int ng = argc - 1; char **gv = argv + 1;
	uint64_t pstr[600]; int ns = 0;
	for (int i = 0; i < ng + ne + 1; i++) {
		const char *s = i < ng ? gv[i] : i < ng + ne ? genv[i - ng] : gv[0];
		sp -= strlen(s) + 1; uc_mem_write(uc, sp, s, strlen(s) + 1); pstr[ns++] = sp;
		if (ns >= 590) break;
	}
	sp &= ~15ULL; np = 0;
	if (dynamic) ptrs[np++] = mh_addr;
	ptrs[np++] = ng;
	for (int i = 0; i < ng; i++) ptrs[np++] = pstr[i];
	ptrs[np++] = 0;
	for (int i = 0; i < ne && ng + i < ns - 1; i++) ptrs[np++] = pstr[ng + i];
	ptrs[np++] = 0; ptrs[np++] = pstr[ns - 1]; ptrs[np++] = 0;
	sp -= np * 8; sp &= ~15ULL; uc_mem_write(uc, sp, ptrs, np * 8);
	uc_reg_write(uc, UC_X86_REG_RSP, &sp);
	uc_hook hm; uc_hook_add(uc, &hm, UC_HOOK_MEM_UNMAPPED, on_unmapped, 0, 1, 0);
	uc_hook h; uc_hook_add(uc, &h, UC_HOOK_INSN, do_syscall, 0, 1, 0, UC_X86_INS_SYSCALL);
	t0 = now();
	e = uc_emu_start(uc, entry, 0, getenv("UCRUN_TIMEOUT_MS") ? atoll(getenv("UCRUN_TIMEOUT_MS")) * 1000 : 0, 0);
	if (!e && getenv("UCRUN_TIMEOUT_MS")) { uint64_t rip; uc_reg_read(uc, UC_X86_REG_RIP, &rip); fprintf(stderr, "ucrun: Zeitlimit, rip %#llx\n", (unsigned long long)rip); }
	if (e) { uint64_t rip; uc_reg_read(uc, UC_X86_REG_RIP, &rip); fprintf(stderr, "ucrun: %s bei rip %#llx\n", uc_strerror(e), (unsigned long long)rip); return 4; }
	return 0;
}
