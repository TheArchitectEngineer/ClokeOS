// a64run – arm64-Mach-O auf x86_64 ausführen, mit Unicorn (QEMU-TCG, AArch64-Kern) als CPU.
//
// Gegenstück zu ucrun (x86_64 auf arm64). Gastspeicher liegt in Unicorns eigenem Adressraum: Seiten des Gastes sind 16 KiB
// (wie bei arm64-Darwin), die Host-Seite (4 KiB) spielt keine Rolle. Systemaufrufe des Gastes (svc #0x80, x16 = Nummer,
// Argumente x0-x7, Fehler als Carry-Flag + errno in x0; BSD-Nummern und LP64-Strukturen sind auf Darwin für x86_64 und arm64 gleich)
// werden mit einer kleinen Tabelle an den Host weitergereicht. Mach-Fallen haben negative x16, die Maschinenfalle
// (thread_set_tsd_base) ist x16 = 0x80000000 mit x3 = 2 und setzt TPIDRRO_EL0.
//
//   a64run <arm64-Mach-O> [Argumente]     (A64_TRACE=1: Systemaufrufe zeigen, A64_WARN=1: nicht umgesetzte melden,
//                                          A64_STATS=1: Zeit/Zähler am Ende, A64_ROOT=<Ordner mit usr/lib/dyld ...>)
#include <errno.h>
#include <fcntl.h>
#include <mach-o/dyld.h>
#include <mach-o/loader.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <unicorn/unicorn.h>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#define PAGE 0x4000ULL                  // Seitengröße des Gastes (arm64-Darwin)
#define STACK_TOP 0x16fe00000ULL        // wie beim arm64-Kernel: Stapel knapp unter 6 GiB
#define STACK_SIZE 0x800000ULL
#define MMAP_BASE 0x280000000ULL
#define DYLD_BASE 0x1f0000000ULL
#define COMMPAGE 0xFFFFFC000ULL         // _COMM_PAGE64_BASE_ADDRESS auf arm64
#define CPU_ARM64 0x0100000c

extern char **environ;
static uc_engine *uc;
static int trace, warn;
static const char *aroot = "";
static unsigned long nsys;
static double t0;
static uint64_t mmap_next = MMAP_BASE;
static char self_exe[1024];

// --- Gastspeicher: Liste der abgebildeten Bereiche, damit MAP_FIXED/munmap auch Teilbereiche und Lücken können ---
typedef struct { uint64_t g, n; } Reg;
static Reg regs[2048]; static int nregs;
static void unmap_range(uint64_t g, uint64_t n) {
	uint64_t ue = g + n;
	for (int i = 0, cnt = nregs; i < cnt; i++) {
		Reg r = regs[i]; uint64_t b = r.g + r.n;
		if (r.g >= ue || b <= g) continue;
		uint64_t s = r.g > g ? r.g : g, e = b < ue ? b : ue;
		uc_mem_unmap(uc, s, e - s);
		if (r.g < s) { regs[i].n = s - r.g; if (b > e && nregs < 2048) regs[nregs++] = (Reg){e, b - e}; }
		else if (b > e) regs[i] = (Reg){e, b - e};
		else regs[i].n = 0;
	}
	int k = 0; for (int i = 0; i < nregs; i++) if (regs[i].n) regs[k++] = regs[i]; nregs = k;
}
static uc_err map_range(uint64_t g, uint64_t n) {
	unmap_range(g, n);
	uc_err e = uc_mem_map(uc, g, n, UC_PROT_ALL);   // Gastspeicher bleibt RWX (wie bei ucrun)
	if (!e && nregs < 2048) regs[nregs++] = (Reg){g, n};
	return e;
}
static uint64_t gmap(uint64_t addr, uint64_t len, int fixed) {
	len = (len + PAGE - 1) & ~(PAGE - 1);
	if (!fixed || !addr) { addr = mmap_next; mmap_next += len + PAGE; }
	else addr &= ~(PAGE - 1);
	if (map_range(addr, len)) return 0;
	return addr;
}
static int gstr(uint64_t a, char *b, size_t n) {
	for (size_t i = 0; i < n; i++) { if (uc_mem_read(uc, a + i, b + i, 1)) return -1; if (!b[i]) return 0; }
	return -1;
}
static char *gdup(uint64_t a) { char b[4096]; if (gstr(a, b, sizeof b)) return 0; return strdup(b); }
static uint64_t X(int i) { uint64_t v; uc_reg_read(uc, UC_ARM64_REG_X0 + i, &v); return v; }
static void setX(int i, uint64_t v) { uc_reg_write(uc, UC_ARM64_REG_X0 + i, &v); }
static void setcarry(int on) { uint64_t f; uc_reg_read(uc, UC_ARM64_REG_NZCV, &f); f = on ? (f | (1u << 29)) : (f & ~(1u << 29)); uc_reg_write(uc, UC_ARM64_REG_NZCV, &f); }
static double now(void) { struct timeval t; gettimeofday(&t, 0); return t.tv_sec + t.tv_usec / 1e6; }

// --- Laden eines arm64-Mach-O (dünn); slide wird auf alle Adressen addiert ---
static int load_image(const uint8_t *file, uint64_t slide, uint64_t *entry, uint64_t *mh_addr, int *dynamic) {
	struct mach_header_64 *mh = (void *)file; uc_err e;
	struct load_command *lc = (void *)(mh + 1);
	for (uint32_t i = 0; i < mh->ncmds; i++, lc = (void *)((char *)lc + lc->cmdsize)) {
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64 *s = (void *)lc;
			if (!strcmp(s->segname, "__PAGEZERO") || !s->vmsize) continue;
			uint64_t va = s->vmaddr + slide, base = va & ~(PAGE - 1), sz = (va + s->vmsize - base + PAGE - 1) & ~(PAGE - 1);
			if ((e = map_range(base, sz))) { fprintf(stderr, "a64run: map %s %#llx: %s\n", s->segname, (unsigned long long)base, uc_strerror(e)); return -1; }
			if (s->filesize) uc_mem_write(uc, va, file + s->fileoff, s->filesize);
			if (!strcmp(s->segname, "__TEXT")) *mh_addr = va;
		} else if (lc->cmd == LC_UNIXTHREAD) {  // flavor, count, x0-x28, fp, lr, sp, pc, cpsr
			*entry = *(uint64_t *)((char *)lc + 16 + 8 * 32) + slide;
		} else if (lc->cmd == LC_LOAD_DYLINKER || lc->cmd == LC_MAIN) *dynamic = 1;
	}
	return 0;
}

// --- MIG: Antworten auf Nachrichten an den Host-Port (wie in ucrun, Layout ist architekturunabhängig) ---
static void mig_reply(uint64_t addr, uint32_t id, uint32_t retcode, const void *payload, uint32_t plen) {
	uint8_t r[512] = {0}; uint32_t bits = 0x12, size = 24 + 8 + 4 + plen, ndr[2] = {0, 1};
	memcpy(r, &bits, 4); memcpy(r + 4, &size, 4); memcpy(r + 20, &id, 4); memcpy(r + 24, ndr, 8); memcpy(r + 32, &retcode, 4);
	if (plen) memcpy(r + 36, payload, plen);
	uc_mem_write(uc, addr, r, size);
}
static void mig_reply_port(uint64_t addr, uint32_t id, uint32_t port) {
	uint8_t r[40] = {0}; uint32_t bits = 0x80000012, size = 40, cnt = 1;
	memcpy(r, &bits, 4); memcpy(r + 4, &size, 4); memcpy(r + 20, &id, 4); memcpy(r + 24, &cnt, 4); memcpy(r + 28, &port, 4);
	r[38] = 17; r[39] = 0;
	uc_mem_write(uc, addr, r, 40);
}

// --- Prozesse ---
static int is_arm64_macho(const char *p) {
	uint32_t h[8]; int fd = open(p, O_RDONLY); if (fd < 0) return 0;
	ssize_t n = read(fd, h, sizeof h); close(fd); if (n < 8) return 0;
	return (h[0] == 0xfeedfacf && h[1] == CPU_ARM64) || h[0] == 0xbebafeca;
}
static const char *guest_path(const char *p, char *buf, size_t n) {  // erst A64_ROOT (arm64-Systembibliotheken), sonst der Host
	if (*aroot && p[0] == '/') { snprintf(buf, n, "%s%s", aroot, p); if (access(buf, F_OK) == 0) return buf; }
	return p;
}
static int do_execve(uint64_t pa, uint64_t av, uint64_t ev) {
	char *path = gdup(pa); if (!path) return EFAULT;
	char rpb[1200]; const char *rp = guest_path(path, rpb, sizeof rpb);
	char *argvv[256], *envv[1024]; int na = 0, ne = 0; uint64_t p;
	for (; na < 250 && !uc_mem_read(uc, av + 8 * na, &p, 8) && p; na++) argvv[na + 1] = gdup(p);
	for (; ne < 1000 && ev && !uc_mem_read(uc, ev + 8 * ne, &p, 8) && p; ne++) envv[ne] = gdup(p);
	envv[ne] = 0;
	if (access(rp, X_OK)) return errno;
	char *nv[260]; int k = 0;
	if (is_arm64_macho(rp)) { nv[k++] = self_exe; nv[k++] = (char *)rp; for (int i = 2; i <= na; i++) nv[k++] = argvv[i]; nv[k] = 0; execve(self_exe, nv, envv); }
	else { for (int i = 1; i <= na; i++) nv[k++] = argvv[i]; nv[k] = 0; execve(rp, nv, envv); }  // x86_64-Programme und #!-Skripte: der Host
	return errno;
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
	{116, "gettimeofday", {{0, 'o', 16, 0}, {1, 'o', 8, 0}}},
	{153, "pread", {{1, 'o', 0, 2}, NONE}},    {154, "pwrite", {{1, 'i', 0, 2}, NONE}},
	{338, "stat64", {{0, 's', 0, 0}, {1, 'o', 144, 0}}}, {340, "lstat64", {{0, 's', 0, 0}, {1, 'o', 144, 0}}},
	{339, "fstat64", {{1, 'o', 144, 0}, NONE}},
	{500, "getentropy", {{0, 'o', 0, 1}, NONE}},
	{327, "issetugid", {NONE, NONE}},
	{344, "getdirentries64", {{1, 'o', 0, 2}, {3, 'o', 8, 0}}},
	{463, "openat", {{1, 's', 0, 0}, NONE}},
	{470, "fstatat64", {{1, 's', 0, 0}, {2, 'o', 144, 0}}},
	{473, "readlinkat", {{1, 's', 0, 0}, {2, 'o', 0, 3}}},
	{58, "readlink", {{0, 's', 0, 0}, {1, 'o', 0, 2}}},
	{346, "fstatfs64", {{1, 'o', 2168, 0}, NONE}},
	{345, "statfs64", {{0, 's', 0, 0}, {1, 'o', 2168, 0}}},
	{60, "umask", {NONE, NONE}},
	{12, "chdir", {{0, 's', 0, 0}, NONE}},
	{13, "fchdir", {NONE, NONE}},
	{15, "chmod", {{0, 's', 0, 0}, NONE}},
	{128, "rename", {{0, 's', 0, 0}, {1, 's', 0, 0}}},
	{57, "symlink", {{0, 's', 0, 0}, {1, 's', 0, 0}}},
	{81, "getpgrp", {NONE, NONE}},
	{82, "setpgid", {NONE, NONE}},
	{194, "getrlimit", {{1, 'o', 16, 0}, NONE}},
	{220, "getattrlist", {{0, 's', 0, 0}, {1, 'i', 24, 0}, {2, 'o', 0, 3}}},
};

// Eigenschaften des Gastes für sysctl: der Host ist x86_64, der Gast sieht arm64 mit 16-KiB-Seiten
static int fake_sysctl_name(const char *name, void *ob, size_t *ol) {
	static const struct { const char *n; uint64_t v; int sz; } v[] = {
		{"hw.pagesize", PAGE, 8}, {"vm.pagesize", PAGE, 8}, {"hw.cputype", CPU_ARM64, 4}, {"hw.cpusubtype", 2, 4}, {"hw.cpu64bit_capable", 1, 4},
		{"hw.cpufamily", 0x1b588bb3, 4}, {"hw.ncpu", 1, 4}, {"hw.activecpu", 1, 4}, {"hw.physicalcpu", 1, 4}, {"hw.logicalcpu", 1, 4},
		{"hw.optional.arm64", 1, 4}, {"hw.optional.floatingpoint", 1, 4}, {"hw.optional.neon", 1, 4}, {"hw.cachelinesize", 64, 8},
	};
	for (unsigned i = 0; i < sizeof v / sizeof *v; i++) if (!strcmp(name, v[i].n)) {
		if (ob && *ol >= (size_t)v[i].sz) memcpy(ob, &v[i].v, v[i].sz);
		*ol = v[i].sz; return 1;
	}
	if (!strcmp(name, "hw.machine") || !strcmp(name, "hw.targettype")) { if (ob && *ol >= 6) memcpy(ob, "arm64", 6); *ol = 6; return 1; }
	return 0;
}


// --- posix_spawn (Systemaufruf 244): Aufbau von struct _posix_spawn_args_desc und der Dateiaktionen wie in ucrun (XNU spawn_internal.h) ---
static void spawn_apply_actions(const uint8_t *fa, size_t fa_size) {
	if (!fa || fa_size < 8) return;
	int count = *(const int *)(fa + 4); const size_t asz = 8 + 1032;  // _psfa_action: type, fd, {oflag, mode, path[1024]}
	for (int i = 0; i < count && 8 + (size_t)(i + 1) * asz <= fa_size; i++) {
		const uint8_t *a = fa + 8 + (size_t)i * asz; int type = *(const int *)a, fd = *(const int *)(a + 4);
		switch (type) {
		case 0: {  // OPEN
			int oflag = *(const int *)(a + 8); unsigned mode = *(const uint16_t *)(a + 12); char p[1100]; strncpy(p, (const char *)(a + 14), 1023); p[1023] = 0;
			char pb[1200]; int nfd = open(guest_path(p, pb, sizeof pb), oflag, mode); if (nfd < 0) _exit(127);
			if (nfd != fd) { dup2(nfd, fd); close(nfd); }
			break; }
		case 1: close(fd); break;
		case 2: { int nfd = *(const int *)(a + 8); if (fd == nfd) { int fl = fcntl(fd, F_GETFD); if (fl >= 0) fcntl(fd, F_SETFD, fl & ~FD_CLOEXEC); } else dup2(fd, nfd); break; }
		case 5: { char p[1100]; strncpy(p, (const char *)(a + 8), 1023); p[1023] = 0; if (chdir(p)) _exit(127); break; }
		case 6: if (fchdir(fd)) _exit(127); break;
		default: break;
		}
	}
}
static uint64_t ring[64]; static unsigned ringn; static int use_ring;
static void on_code(uc_engine *u, uint64_t addr, uint32_t size, void *ud) { (void)u; (void)size; (void)ud; ring[ringn++ & 63] = addr; }
static void dump_ring(void) {
	if (!use_ring) return;
	fprintf(stderr, "  letzte Befehle (alt -> neu):");
	for (unsigned q = ringn > 40 ? ringn - 40 : 0; q < ringn; q++) { uint32_t w = 0; uc_mem_read(uc, ring[q & 63], &w, 4); fprintf(stderr, " %llx:%08x", (unsigned long long)ring[q & 63], w); }
	fputc(10, stderr);
}
static void do_syscall(uc_engine *u, uint32_t intno, void *ud) {
	(void)u; (void)ud;
	if (intno != 2) {
		uint64_t pc; uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
		uint32_t insn = 0; uc_mem_read(uc, pc, &insn, 4);
		fprintf(stderr, "a64run: Ausnahme %u bei pc %#llx, Befehl %08x, x0-x3 %#llx %#llx %#llx %#llx\n", intno, (unsigned long long)pc, insn,
			(unsigned long long)X(0), (unsigned long long)X(1), (unsigned long long)X(2), (unsigned long long)X(3));
		fprintf(stderr, "  x8-x12 %#llx %#llx %#llx %#llx %#llx\n", (unsigned long long)X(8), (unsigned long long)X(9), (unsigned long long)X(10), (unsigned long long)X(11), (unsigned long long)X(12)); dump_ring(); uc_emu_stop(uc); return;
	}
	uint64_t x16 = X(16); if (x16 != 0x80000000ULL && (x16 >> 32) == 0 && (int32_t)x16 < 0) x16 = (uint64_t)(int64_t)(int32_t)x16;  // mov w16, #-3 füllt die obere Hälfte mit 0
	uint64_t a[8], x1v = 0; int64_t res = 0; int err = 0; nsys++;
	for (int i = 0; i < 8; i++) a[i] = X(i);
	if ((int64_t)x16 < 0) {  // Mach-Fallen
		int64_t tn = -(int64_t)x16;
		if (trace) fprintf(stderr, "a64run: mach trap %lld(%#llx, %#llx, %#llx, %#llx, %#llx, %#llx)\n", (long long)tn, (unsigned long long)a[0], (unsigned long long)a[1], (unsigned long long)a[2], (unsigned long long)a[3], (unsigned long long)a[4], (unsigned long long)a[5]);
		switch (tn) {
		case 3: { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); res = ts.tv_sec * 1000000000ULL + ts.tv_nsec; break; }  // mach_absolute_time (Commpage ohne Zeitbasis)
		case 4: { struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts); res = ts.tv_sec * 1000000000ULL + ts.tv_nsec; break; }  // mach_continuous_time
		case 26: res = 0x303; break;   // mach_reply_port
		case 27: res = 0x203; break;   // thread_self_trap
		case 28: res = 0x103; break;   // task_self_trap
		case 29: res = 0x403; break;   // host_self_trap
		case 10: {                     // _kernelrpc_mach_vm_allocate_trap(task, *addr, size, flags)
			uint64_t want = 0; uc_mem_read(uc, a[1], &want, 8);
			uint64_t g = gmap((a[3] & 1) ? 0 : want, a[2], !(a[3] & 1) && want);
			if (!g) { res = 3; break; }
			uc_mem_write(uc, a[1], &g, 8); break; }
		case 89: { uint32_t tb[2] = {1, 1}; uc_mem_write(uc, a[0], tb, 8); break; }  // mach_timebase_info_trap
		case 15: {                     // _kernelrpc_mach_vm_map_trap(task, *addr, size, mask, flags, prot)
			uint64_t al = a[3] + 1; if (al > 1) mmap_next = (mmap_next + al - 1) & ~(al - 1);
			uint64_t g = gmap(0, a[2], 0);
			if (!g) { res = 3; break; }
			uc_mem_write(uc, a[1], &g, 8); break; }
		case 16: { uint32_t nm = 0x1003; uc_mem_write(uc, a[2], &nm, 4); break; }  // mach_port_allocate_trap(task, right, *name)
		case 12: unmap_range(a[1] & ~(PAGE - 1), (a[2] + PAGE - 1) & ~(PAGE - 1)); break;  // mach_vm_deallocate
		case 14: break;                // mach_vm_protect: Gastspeicher bleibt RWX
		case 18: case 19: break;       // mach_port_deallocate / mod_refs
		case 31: {                     // mach_msg_trap: nur Nachrichten an den Host-Port, als kleine MIG-Server
			uint8_t m[0x40]; uc_mem_read(uc, a[0], m, 0x40); uint32_t id = *(uint32_t *)(m + 20);
			if (trace) fprintf(stderr, "a64run: mach_msg id %u an Port %#x flavor %u count %u\n", id, *(uint32_t *)(m + 8), *(uint32_t *)(m + 32), *(uint32_t *)(m + 36));
			if (id == 200) {  // host_info(flavor, count)
				uint32_t flavor = *(uint32_t *)(m + 32);
				if (flavor == 1) {  // HOST_BASIC_INFO
					uint32_t p[1 + 12] = {12, 1, 1, 4096, CPU_ARM64, 2, 0, 1, 1, 1, 1, 0, 1};
					p[11] = 0; p[12] = 1;
					mig_reply(a[0], 300, 0, p, sizeof p);
				} else if (flavor == 5) {
					uint32_t p[1 + 8] = {8, 81, 80, 80, 31, 0, 0, 0, 127};
					mig_reply(a[0], 300, 0, p, sizeof p);
				} else mig_reply(a[0], 300, 4, 0, 0);
			} else if (id == 3409) { mig_reply_port(a[0], 3509, 0);   // task_get_special_port: kein Bootstrap-Server -> MACH_PORT_NULL
			} else if (id == 3418) { static uint32_t sport = 0x5003; mig_reply_port(a[0], 3518, sport); sport += 4;  // semaphore_create (Threads fehlen noch: nur ein Name)
			} else if (id == 3419) { mig_reply(a[0], 3519, 0, 0, 0);  // semaphore_destroy
			} else if (id == 206) { mig_reply_port(a[0], 306, 0x1004); // host_get_clock_service
			} else { if (trace || warn) fprintf(stderr, "a64run: mach_msg id %u nicht umgesetzt\n", id); mig_reply(a[0], id + 100, 5, 0, 0); }
			break; }
		default: if (trace || warn) fprintf(stderr, "a64run: Mach-Falle %lld nicht umgesetzt\n", (long long)tn); res = 5;
		}
		setX(0, res); return;
	}
	if (x16 == 0x80000000ULL) {  // Maschinenfalle: x3 = 2 thread_set_tsd_base(x0)
		if (X(3) == 2) { uc_reg_write(uc, UC_ARM64_REG_TPIDRRO_EL0, &a[0]); res = 0; }
		else { if (trace || warn) fprintf(stderr, "a64run: Maschinenfalle %llu nicht umgesetzt\n", (unsigned long long)X(3)); res = 0; }
		setX(0, res); return;
	}
	uint64_t nr = x16;
	{ static const struct { int from, to; } nc[] = {{423, 334}, {396, 3}, {397, 4}, {398, 5}, {399, 6}, {400, 7}, {406, 92}, {410, 111}, {414, 153}, {415, 154}};  // *_nocancel-Varianten
	  for (unsigned q = 0; q < sizeof nc / sizeof *nc; q++) if (nc[q].from == (int)nr) { nr = nc[q].to; break; } }
	if (trace) fprintf(stderr, "a64run: sys %llu(%#llx, %#llx, %#llx)\n", (unsigned long long)nr, (unsigned long long)a[0], (unsigned long long)a[1], (unsigned long long)a[2]);
	if (nr == 1) {
		if (getenv("A64_STATS")) fprintf(stderr, "a64run: %.3f s, %lu Systemaufrufe\n", now() - t0, nsys);
		exit((int)a[0]);
	}
	if (nr == 197) {  // mmap(addr, len, prot, flags, fd, off): anonym oder Datei (Inhalt wird kopiert)
		uint64_t g = gmap(a[0], a[1], (a[3] & MAP_FIXED) != 0);
		if (!g) { err = ENOMEM; res = ENOMEM; goto done; }
		if (!(a[3] & MAP_ANON)) {
			char *b = malloc(a[1]); ssize_t n = pread((int)a[4], b, a[1], (off_t)a[5]);
			if (n > 0) uc_mem_write(uc, g, b, n);
			free(b);
		}
		res = g; goto done;
	}
	switch (nr) {  // Aufrufe, die nur beim Start nach dem Zustand des Systems fragen
	case 483: case 381: case 520: case 521: case 331: res = 0; goto done;                 // csrctl, __mac_syscall ...
	case 366: res = 0; goto done;                                                         // bsdthread_register (Threads: noch nicht)
	case 54: err = 25; res = 25; goto done;                              // ioctl: ENOTTY
	case 294: case 336: case 287: err = EINVAL; res = EINVAL; goto done; // shared_region_check_np (kein Cache), proc_info
	case 46: case 48: case 329: case 53: res = 0; goto done;             // Signale: noch ohne Wirkung
	}
	if (nr == 274) {  // __sysctlbyname(name, namelen, oldp, oldlenp, newp, newlen)
		char nm[256] = {0}; uint64_t nl = a[1] < 255 ? a[1] : 255; uc_mem_read(uc, a[0], nm, nl);
		size_t ol = 0; if (a[3]) uc_mem_read(uc, a[3], &ol, 8);
		char ob[512] = {0}; size_t cap = ol < sizeof ob ? ol : sizeof ob; size_t l2 = cap;
		if (a[4]) { err = EPERM; res = EPERM; goto done; }
		if (fake_sysctl_name(nm, ob, &l2)) { res = 0; if (a[2] && l2 > ol) { err = ENOMEM; res = ENOMEM; } }
		else { l2 = cap; res = sysctlbyname(nm, a[2] ? ob : 0, &l2, 0, 0); if (res) { err = errno; res = errno; } }
		if (!err && a[2]) uc_mem_write(uc, a[2], ob, l2 < sizeof ob ? l2 : sizeof ob);
		if (a[3] && (!err || err == ENOMEM)) uc_mem_write(uc, a[3], &l2, 8);
		goto done;
	}
	if (nr == 92) {  // fcntl: Code-Signatur-Befehle gibt es hier nicht
		int cmd = (int)a[1];
		if (cmd == 98) { res = 0; goto done; }                                  // F_CHECK_LV
		if (cmd == 59 || cmd == 61 || cmd == 97 || cmd == 99) { err = 45; res = 45; goto done; }  // F_ADDFILESIGS*: ENOTSUP
		if (cmd == 50) { char p[1024] = {0}; res = fcntl((int)a[0], 50, p); if (res == -1) { err = errno; res = errno; } else uc_mem_write(uc, a[2], p, 1024); goto done; }
		res = fcntl((int)a[0], cmd, a[2]); if (res == -1) { err = errno; res = errno; } goto done;
	}
	if (nr == 202) {  // sysctl(name, namelen, oldp, oldlenp, newp, newlen)
		int mib[16]; uint32_t nl = a[1] > 16 ? 16 : a[1]; uc_mem_read(uc, a[0], mib, nl * 4);
		size_t ol = 0; if (a[3]) uc_mem_read(uc, a[3], &ol, 8);
		if (trace) fprintf(stderr, "a64run: sysctl mib %d.%d.%d len %u oldlen %zu\n", mib[0], nl > 1 ? mib[1] : 0, nl > 2 ? mib[2] : 0, nl, ol);
		char *ob = a[2] ? calloc(1, ol ? ol : 1) : 0; void *nb = 0;
		if (a[4]) { nb = malloc(a[5]); uc_mem_read(uc, a[4], nb, a[5]); }
		if (nl >= 2 && mib[0] == 6 && mib[1] == 1 && ob && ol >= 6) { memcpy(ob, "arm64", 6); ol = 6; res = 0; }          // hw.machine
		else if (nl >= 2 && mib[0] == 6 && mib[1] == 7 && ob && ol >= 8) { uint64_t ps = PAGE; if (ol >= 8) memcpy(ob, &ps, 8); ol = 8; res = 0; }  // hw.pagesize
		else res = sysctl(mib, nl, ob, a[3] ? &ol : 0, nb, a[5]);
		if (res == -1) { err = errno; res = errno; }
		if (ob && (res == 0 || err == ENOMEM)) uc_mem_write(uc, a[2], ob, ol);
		if (a[3] && (res == 0 || err == ENOMEM)) uc_mem_write(uc, a[3], &ol, 8);
		free(ob); free(nb); goto done;
	}
	if (nr == 2 || nr == 66) {  // fork/vfork: Host-fork; Kind: x1 = 1 (Darwin-Konvention)
		fflush(0); pid_t pid = fork();
		if (pid < 0) { err = errno; res = errno; goto done; }
		res = pid; x1v = pid == 0 ? 1 : 0;
		if (pid == 0) nsys = 0;
		goto done;
	}
	if (nr == 244) {  // posix_spawn(pid_t *pid, path, adesc, argv, envp): fork + Dateiaktionen + execve (SETEXEC: ohne fork)
		uint64_t d[4] = {0, 0, 0, 0}; if (a[2]) uc_mem_read(uc, a[2], d, 32);
		uint8_t attr[64] = {0}; if (d[1]) uc_mem_read(uc, d[1], attr, d[0] < 64 ? d[0] : 64);
		short aflags = *(short *)attr; uint8_t *fa = 0; if (d[3] && d[2] > 0 && d[2] < (1u << 24)) { fa = malloc(d[2]); uc_mem_read(uc, d[3], fa, d[2]); }
		char *pth = gdup(a[1]); if (!pth) { err = EFAULT; res = EFAULT; goto done; }
		char rpb[1200]; const char *rp = guest_path(pth, rpb, sizeof rpb);
		if (access(rp, X_OK)) { err = errno; res = errno; free(fa); goto done; }  // Fehler beim Starten melden wie der Kernel
		if (aflags & 0x40) { spawn_apply_actions(fa, d[2]); int r = do_execve(a[1], a[3], a[4]); err = r; res = r; goto done; }
		fflush(0); pid_t pid = fork();
		if (pid < 0) { err = errno; res = errno; free(fa); goto done; }
		if (pid == 0) {
			spawn_apply_actions(fa, d[2]);
			if (aflags & 2) setpgid(0, *(pid_t *)(attr + 12));
			if (aflags & 4) { uint32_t sd = *(uint32_t *)(attr + 4); for (int sg = 1; sg < 32; sg++) if (sd & (1u << (sg - 1))) signal(sg, SIG_DFL); }
			do_execve(a[1], a[3], a[4]); _exit(127);
		}
		free(fa);
		if (a[0]) { int32_t p32 = pid; uc_mem_write(uc, a[0], &p32, 4); }
		res = 0; goto done;
	}
	if (nr == 59) { int r = do_execve(a[0], a[1], a[2]); err = r; res = r; goto done; }  // execve kehrt nur bei Fehler zurück
	if (nr == 7) {  // wait4(pid, *status, options, *rusage)
		int st = 0; struct rusage ru; pid_t r = wait4((pid_t)a[0], &st, (int)a[2], a[3] ? &ru : 0);
		if (r < 0) { err = errno; res = errno; goto done; }
		if (a[1]) uc_mem_write(uc, a[1], &st, 4);
		if (a[3]) uc_mem_write(uc, a[3], &ru, sizeof ru);
		res = r; goto done;
	}
	if (nr == 42) { int fds[2]; if (pipe(fds)) { err = errno; res = errno; goto done; } res = fds[0]; x1v = fds[1]; goto done; }  // pipe: x0 = Lese-, x1 = Schreibende
	if (nr == 90) { res = dup2((int)a[0], (int)a[1]); if (res < 0) { err = errno; res = errno; } goto done; }
	if (nr == 39) { res = getppid(); goto done; }
	if (nr == 37) { res = kill((pid_t)a[0], (int)a[1]); if (res < 0) { err = errno; res = errno; } goto done; }
	if (nr == 372) { res = 1; goto done; }  // thread_selfid
	if (nr == 73) { unmap_range(a[0] & ~(PAGE - 1), (a[1] + PAGE - 1) & ~(PAGE - 1)); res = 0; goto done; }
	if (nr == 74) { res = 0; goto done; }   // mprotect: Gastspeicher bleibt RWX
	const Sc *s = 0;
	for (unsigned i = 0; i < sizeof tab / sizeof *tab; i++) if (tab[i].nr == (int)nr) s = &tab[i];
	if (!s) { if (trace || warn) fprintf(stderr, "a64run: Systemaufruf %llu nicht umgesetzt\n", (unsigned long long)nr); err = ENOSYS; res = ENOSYS; goto done; }
	uint64_t h[6]; memcpy(h, a, sizeof h);
	char *bufs[3] = {0, 0, 0}; size_t sz[3] = {0, 0, 0}; char pathb[3][1100];
	for (int k = 0; k < 3; k++) {
		int i = s->p[k].idx; if (i < 0 || !s->p[k].dir) continue;
		if (s->p[k].dir == 's') {
			char raw[1024]; if (gstr(a[i], raw, sizeof raw)) { err = EFAULT; res = EFAULT; goto done; }
			strcpy(pathb[k], guest_path(raw, pathb[k], sizeof pathb[k]));
			h[i] = (uint64_t)pathb[k]; continue; }
		sz[k] = s->p[k].fixed ? (size_t)s->p[k].fixed : (size_t)a[s->p[k].size_arg];
		bufs[k] = calloc(1, sz[k] ? sz[k] : 1);
		if (s->p[k].dir == 'i') uc_mem_read(uc, a[i], bufs[k], sz[k]);
		h[i] = (uint64_t)bufs[k];
	}
	if (nr == 338) res = stat((char *)h[0], (struct stat *)h[1]);
	else if (nr == 340) res = lstat((char *)h[0], (struct stat *)h[1]);
	else if (nr == 339) res = fstat((int)h[0], (struct stat *)h[1]);
	else if (nr == 470) res = fstatat((int)h[0], (char *)h[1], (struct stat *)h[2], (int)h[3]);
	else res = syscall((int)nr, h[0], h[1], h[2], h[3], h[4], h[5]);
	if (res == -1) { err = errno; res = errno; }
	else for (int k = 0; k < 3; k++) if (bufs[k] && s->p[k].dir == 'o') {
		size_t n = sz[k]; if ((nr == 3 || nr == 153 || nr == 344) && k == 0) n = res < 0 ? 0 : (size_t)res;
		else if ((nr == 473 || nr == 58) && res >= 0) n = (size_t)res;
		uc_mem_write(uc, a[s->p[k].idx], bufs[k], n);
	}
	for (int k = 0; k < 3; k++) free(bufs[k]);
done:
	setcarry(err != 0);
	setX(0, res); if (nr == 2 || nr == 66 || nr == 42) setX(1, x1v);
}

static bool on_fault(uc_engine *u, uc_mem_type t, uint64_t addr, int size, int64_t val, void *ud) {
	(void)u; (void)val; (void)ud;
	fprintf(stderr, "a64run: Zugriff auf nicht abgebildeten Gastspeicher: Art %d, Adresse %#llx, Größe %d\n", (int)t, (unsigned long long)addr, size);
	return false;
}
static bool on_insn_sys(uc_engine *u, uint32_t reg, const uc_arm64_cp_reg *cp, void *ud) {  // mrs/msr auf Systemregister, die Unicorn nicht kennt
	(void)u; (void)ud;
	fprintf(stderr, "a64run: unbekanntes Systemregister op0=%u op1=%u crn=%u crm=%u op2=%u\n", cp->op0, cp->op1, cp->crn, cp->crm, cp->op2);
	(void)reg; return false;
}

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: a64run <arm64 Mach-O> [args]\n"); return 2; }
	trace = getenv("A64_TRACE") != 0; warn = getenv("A64_WARN") != 0;
	{ uint32_t sz = sizeof self_exe; if (_NSGetExecutablePath(self_exe, &sz)) strcpy(self_exe, argv[0]); }
	if (getenv("A64_ROOT")) aroot = getenv("A64_ROOT");
	else if (access("/Library/ClokeOS/arm64/usr/lib/dyld", F_OK) == 0) aroot = "/Library/ClokeOS/arm64";
	FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 127; }
	fseek(f, 0, SEEK_END); long fl = ftell(f); fseek(f, 0, SEEK_SET);
	uint8_t *file = malloc(fl); if (fread(file, 1, fl, f) != (size_t)fl) return 2;
	if (*(uint32_t *)file == 0xbebafeca) {  // fat: arm64-Scheibe suchen (Kopf big endian), Unterart 0 (nicht arm64e)
		uint32_t na = __builtin_bswap32(*(uint32_t *)(file + 4)), off = 0;
		for (uint32_t i = 0; i < na; i++) if (__builtin_bswap32(*(uint32_t *)(file + 8 + 20 * i)) == CPU_ARM64 && !off) off = __builtin_bswap32(*(uint32_t *)(file + 16 + 20 * i));
		if (off) file += off;
	}
	struct mach_header_64 *mh = (void *)file;
	if (mh->magic != MH_MAGIC_64 || mh->cputype != (int)CPU_ARM64) { fprintf(stderr, "a64run: %s ist kein arm64-Mach-O\n", argv[1]); return 126; }
	uc_err e = uc_open(UC_ARCH_ARM64, UC_MODE_ARM, &uc); if (e) { fprintf(stderr, "uc_open: %s\n", uc_strerror(e)); return 3; }
	{ uint64_t cpacr = 3 << 20; uc_reg_write(uc, UC_ARM64_REG_CPACR_EL1, &cpacr); }  // Gleitkomma/NEON im EL0 freigeben
	uc_hook h1, h2, h3;
	uc_hook_add(uc, &h1, UC_HOOK_INTR, do_syscall, 0, 1, 0);
	uc_hook_add(uc, &h2, UC_HOOK_MEM_UNMAPPED, on_fault, 0, 1, 0);
	(void)h3; (void)on_insn_sys;
	if (getenv("A64_RING")) { use_ring = 1; uc_hook ch; uc_hook_add(uc, &ch, UC_HOOK_CODE, on_code, 0, 1, 0); }
	uint64_t entry = 0, mh_addr = 0; int dynamic = 0;
	if (load_image(file, 0, &entry, &mh_addr, &dynamic)) return 3;
	if (dynamic) {  // wie der Kernel: dyld des Gastes laden, es lädt Programm und Bibliotheken selbst
		char dp[1100]; snprintf(dp, sizeof dp, "%s/usr/lib/dyld", aroot);
		FILE *df = fopen(dp, "rb"); if (!df) { fprintf(stderr, "a64run: %s fehlt (A64_ROOT mit arm64-Systembibliotheken)\n", dp); return 125; }
		fseek(df, 0, SEEK_END); long dl = ftell(df); fseek(df, 0, SEEK_SET);
		uint8_t *dfile = malloc(dl); if (fread(dfile, 1, dl, df) != (size_t)dl) return 2;
		uint64_t dmh = 0, dentry = 0; int dd = 0;
		if (load_image(dfile, DYLD_BASE, &dentry, &dmh, &dd)) return 3;
		entry = dentry;
	}
	{  // Commpage von arm64-Darwin (0xFFFFFC000): nur die Felder, die libSystem liest
		uint8_t cp[PAGE]; memset(cp, 0, sizeof cp); uint64_t caps = 0x1fULL; uint16_t ver = 3; uint8_t ncpu = 1; uint64_t mem = 4ULL << 30;
		memcpy(cp, "commpage 64-bit", 16); memcpy(cp + 0x10, &caps, 8); memcpy(cp + 0x1e, &ver, 2); cp[0x22] = ncpu;
		cp[0x34] = cp[0x35] = cp[0x36] = ncpu; memcpy(cp + 0x38, &mem, 8);
		cp[0x24] = cp[0x25] = cp[0x37] = 14;  // Seitengröße 16 KiB: Benutzer-Seitenverschiebung (32/64 Bit) und Kern-Seitenverschiebung; libSystem leitet vm_page_size daraus ab
		// +0x90 = user_timebase: 0 -> libSystem fragt den Kern (Falle -3), +0xC120.. gettimeofday-Daten bleiben 0 (Rückfall auf den Systemaufruf)
		map_range(COMMPAGE, PAGE); uc_mem_write(uc, COMMPAGE, cp, sizeof cp);
	}
	map_range(STACK_TOP - STACK_SIZE, STACK_SIZE);
	// Stapel wie der Kernel: [mach_header*], argc, argv[], 0, envp[], 0, apple[], 0, Strings
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
	sp &= ~15ULL;
	if (dynamic) ptrs[np++] = mh_addr;
	ptrs[np++] = ng;
	for (int i = 0; i < ng; i++) ptrs[np++] = pstr[i];
	ptrs[np++] = 0;
	for (int i = 0; i < ne && ng + i < ns - 1; i++) ptrs[np++] = pstr[ng + i];
	ptrs[np++] = 0; ptrs[np++] = pstr[ns - 1]; ptrs[np++] = 0;
	sp -= np * 8; sp &= ~15ULL; uc_mem_write(uc, sp, ptrs, np * 8);
	uc_reg_write(uc, UC_ARM64_REG_SP, &sp);
	t0 = now();
	e = uc_emu_start(uc, entry, 0, 0, 0);
	uint64_t pc; uc_reg_read(uc, UC_ARM64_REG_PC, &pc);
	fprintf(stderr, "a64run: %s bei pc %#llx\n", uc_strerror(e), (unsigned long long)pc);
	return 4;
}
