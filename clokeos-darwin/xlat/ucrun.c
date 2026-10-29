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
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec / 1e6; }

static int gstr(uint64_t a, char *b, size_t n) {  // C-String aus dem Gast
	for (size_t i = 0; i < n; i++) { if (uc_mem_read(uc, a + i, b + i, 1)) return -1; if (!b[i]) return 0; }
	return -1;
}

// Gastzeiger-Beschreibung eines Aufrufs: arg index, Richtung ('i' rein, 'o' raus, 's' Pfad), Größe fest oder aus Argument
typedef struct { int nr; const char *name; struct { int idx; char dir; int fixed, size_arg; } p[2]; } Sc;
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
	if (nr == 73) { uc_mem_unmap(u, a[0], (a[1] + 0xfff) & ~0xfffULL); res = 0; goto done; }
	if (nr == 74) { res = 0; goto done; }  // mprotect: Gastspeicher bleibt RWX (Eigenschaft von ucrun, siehe Studie)
	const Sc *s = 0;
	for (unsigned i = 0; i < sizeof tab / sizeof *tab; i++) if (tab[i].nr == (int)nr) s = &tab[i];
	if (!s) { fprintf(stderr, "ucrun: Systemaufruf %llu nicht umgesetzt\n", (unsigned long long)nr); err = ENOSYS; res = ENOSYS; goto done; }
	uint64_t h[6]; memcpy(h, a, sizeof h);
	char *bufs[2] = {0, 0}; size_t sz[2] = {0, 0}; char pathb[2][1024];
	for (int k = 0; k < 2; k++) {
		int i = s->p[k].idx; if (i < 0) continue;
		if (s->p[k].dir == 's') { if (gstr(a[i], pathb[k], 1024)) { err = EFAULT; res = EFAULT; goto done; } h[i] = (uint64_t)pathb[k]; continue; }
		sz[k] = s->p[k].fixed ? (size_t)s->p[k].fixed : (size_t)a[s->p[k].size_arg];
		bufs[k] = calloc(1, sz[k] ? sz[k] : 1);
		if (s->p[k].dir == 'i') uc_mem_read(u, a[i], bufs[k], sz[k]);
		h[i] = (uint64_t)bufs[k];
	}
	if (nr == 339 && 0) {}
	res = syscall((int)nr, h[0], h[1], h[2], h[3], h[4], h[5]);
	if (res == -1) { err = errno; res = errno; }
	else for (int k = 0; k < 2; k++) if (bufs[k] && s->p[k].dir == 'o') {
		size_t n = sz[k]; if (nr == 3 || nr == 153) n = res < 0 ? 0 : (size_t)res;
		uc_mem_write(u, a[s->p[k].idx], bufs[k], n);
	}
	for (int k = 0; k < 2; k++) free(bufs[k]);
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
	uint64_t entry = 0;
	struct load_command *lc = (void *)(mh + 1);
	for (uint32_t i = 0; i < mh->ncmds; i++, lc = (void *)((char *)lc + lc->cmdsize)) {
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64 *s = (void *)lc;
			if (!strcmp(s->segname, "__PAGEZERO") || !s->vmsize) continue;
			uint64_t base = s->vmaddr & ~0xfffULL, sz = (s->vmaddr + s->vmsize - base + 0xfff) & ~0xfffULL;
			uc_mem_unmap(uc, base, sz);
			if ((e = uc_mem_map(uc, base, sz, UC_PROT_ALL)) && e != UC_ERR_MAP) { fprintf(stderr, "map %s: %s\n", s->segname, uc_strerror(e)); return 3; }
			uc_mem_write(uc, s->vmaddr, file + s->fileoff, s->filesize);
		} else if (lc->cmd == LC_UNIXTHREAD) {
			entry = *(uint64_t *)((uint32_t *)((char *)lc + 8) + 2 + 16 * 2);
		} else if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_LOAD_DYLINKER || lc->cmd == LC_MAIN) {
			fprintf(stderr, "ucrun: dynamisches Programm (dyld/libSystem nötig): noch nicht unterstützt\n"); return 125;
		}
	}
	uc_mem_map(uc, STACK_TOP - STACK_SIZE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
	// Stapel wie ihn der Kernel für ein Programm mit LC_UNIXTHREAD baut: argc, argv[], 0, envp[], 0, apple[], 0, Strings
	int ne = 0; while (environ[ne]) ne++;
	uint64_t sp = STACK_TOP - 0x1000, ptrs[512]; int np = 0;
	int ng = argc - 1; char **gv = argv + 1;
	uint64_t pstr[600]; int ns = 0;
	for (int i = 0; i < ng + ne + 1; i++) {
		const char *s = i < ng ? gv[i] : i < ng + ne ? environ[i - ng] : gv[0];
		sp -= strlen(s) + 1; uc_mem_write(uc, sp, s, strlen(s) + 1); pstr[ns++] = sp;
		if (ns >= 590) break;
	}
	sp &= ~15ULL; np = 0;
	ptrs[np++] = ng;
	for (int i = 0; i < ng; i++) ptrs[np++] = pstr[i];
	ptrs[np++] = 0;
	for (int i = 0; i < ne && ng + i < ns - 1; i++) ptrs[np++] = pstr[ng + i];
	ptrs[np++] = 0; ptrs[np++] = pstr[ns - 1]; ptrs[np++] = 0;
	sp -= np * 8; sp &= ~15ULL; uc_mem_write(uc, sp, ptrs, np * 8);
	uc_reg_write(uc, UC_X86_REG_RSP, &sp);
	uc_hook h; uc_hook_add(uc, &h, UC_HOOK_INSN, do_syscall, 0, 1, 0, UC_X86_INS_SYSCALL);
	t0 = now();
	e = uc_emu_start(uc, entry, 0, 0, 0);
	if (e) { uint64_t rip; uc_reg_read(uc, UC_X86_REG_RIP, &rip); fprintf(stderr, "ucrun: %s bei rip %#llx\n", uc_strerror(e), (unsigned long long)rip); return 4; }
	return 0;
}
