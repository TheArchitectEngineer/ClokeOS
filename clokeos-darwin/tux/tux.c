// tux – Linux-Programme (ELF, gleiche Architektur) auf ClokeOS (Darwin/XNU) ausführen.
//
// Ablauf: tux lädt die ELF-Datei (und bei dynamischen Programmen den Lader, z. B. musls
// ld-musl-*.so.1) in den eigenen Adressraum, baut Stapel und Hilfsvektor wie ein Linux-Kernel und
// springt hinein. Jeder Systemaufruf-Befehl des Programms wird beim Laden durch einen Haltepunkt
// ersetzt (binäres Umschreiben, Wege dafür siehe README); der Signal-Handler übersetzt den Aufruf
// in einen Darwin-Aufruf (sys.c) und setzt das Programm hinter dem Befehl fort.
#include "tux.h"
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <unistd.h>

size_t tux_page;
char tux_root[PATH_MAX] = "/TUX";
char tux_cwd[PATH_MAX] = "/";
char tux_exe[PATH_MAX];
char tux_self[PATH_MAX];
int tux_trace;

#define AT_NULL 0
#define AT_PHDR 3
#define AT_PHENT 4
#define AT_PHNUM 5
#define AT_PAGESZ 6
#define AT_BASE 7
#define AT_FLAGS 8
#define AT_ENTRY 9
#define AT_UID 11
#define AT_EUID 12
#define AT_GID 13
#define AT_EGID 14
#define AT_PLATFORM 15
#define AT_HWCAP 16
#define AT_CLKTCK 17
#define AT_SECURE 23
#define AT_RANDOM 25
#define AT_HWCAP2 26
#define AT_EXECFN 31

typedef struct {
	unsigned char ident[16];
	uint16_t type, machine;
	uint32_t version;
	uint64_t entry, phoff, shoff;
	uint32_t flags;
	uint16_t ehsize, phentsize, phnum, shentsize, shnum, shstrndx;
} Ehdr;
typedef struct {
	uint32_t type, flags;
	uint64_t offset, vaddr, paddr, filesz, memsz, align;
} Phdr;
#define PT_LOAD 1
#define PT_INTERP 3
#define PT_PHDR 6

typedef struct {
	uint64_t entry, phdr, phnum, phent, base;
	char interp[PATH_MAX];
} elfinfo;

static void die(const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
#include <stdarg.h>
static void die(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "tux: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
	_exit(127);
}

// ---------------------------------------------------------------------------------------------
// Thread-Zeiger (TLS). Der Kern von PureDarwin setzt TPIDR_EL0 bei jedem Taskwechsel auf 0, ein
// Linux-Programm kann ihn also nicht behalten; stattdessen liegt der Wert in einem Darwin-TSD-Platz
// (pthread_setspecific), den man mit zwei Befehlen über TPIDRRO_EL0 lesen kann.
static pthread_key_t tp_key;

void tux_set_tp(uint64_t tp) { pthread_setspecific(tp_key, (void *)tp); }
uint64_t tux_get_tp(void) { return (uint64_t)pthread_getspecific(tp_key); }

// ---------------------------------------------------------------------------------------------
// Systemaufruf-Befehle umschreiben
#ifdef TUX_ARM64
#define BRK(imm) (0xd4200000u | ((uint32_t)(imm) << 5))
#define IMM_SVC 0x7100
#define IMM_MRS 0x7200   // + Zielregister: mrs xN, tpidr_el0
#define IMM_MSR 0x7300   // + Quellregister: msr tpidr_el0, xN

void tux_patch_range(void *addr, size_t len)
{
	uint32_t *p = (uint32_t *)(((uintptr_t)addr + 3) & ~3UL), *e = (uint32_t *)((uintptr_t)addr + len);
	for (; p + 1 <= e; p++) {
		uint32_t w = *p;
		if (w == 0xd4000001u) *p = BRK(IMM_SVC);
		else if ((w & 0xffffffe0u) == 0xd53bd040u) *p = BRK(IMM_MRS | (w & 31));
		else if ((w & 0xffffffe0u) == 0xd51bd040u) *p = BRK(IMM_MSR | (w & 31));
	}
	__builtin___clear_cache((char *)addr, (char *)addr + len);
}

static void on_trap(int sig, siginfo_t *si, void *ucv)
{
	ucontext_t *uc = ucv;
	_STRUCT_ARM_THREAD_STATE64 *ss = &uc->uc_mcontext->__ss;
	uint32_t insn = *(uint32_t *)ss->__pc;
	if ((insn & 0xffe0001fu) != 0xd4200000u) {
		// kein Haltepunkt von uns: echter Fehler des Programms
		fprintf(stderr, "tux: Signal %d bei pc=%llx (unbekannter Befehl %08x)\n", sig, ss->__pc, insn);
		_exit(128 + sig);
	}
	unsigned imm = (insn >> 5) & 0xffff;
	if (imm == IMM_SVC) {
		tux_regs r;
		r.nr = ss->__x[8];
		for (int i = 0; i < 6; i++) r.a[i] = ss->__x[i];
		uint64_t ret = 0;
		r.ret = &ret;
		r.sp = (uint64_t *)&ss->__sp;
		r.pc = (uint64_t *)&ss->__pc;
		r.uc = uc;
		ret = ss->__x[0];
		long v = tux_syscall(&r);
		ss->__x[0] = (uint64_t)v;
	} else if ((imm & 0xff00) == IMM_MRS) {
		unsigned rt = imm & 31;
		if (rt < 29) ss->__x[rt] = tux_get_tp();
		else if (rt == 29) ss->__fp = tux_get_tp();
		else if (rt == 30) ss->__lr = tux_get_tp();
	} else if ((imm & 0xff00) == IMM_MSR) {
		unsigned rt = imm & 31;
		uint64_t v = rt < 29 ? ss->__x[rt] : rt == 29 ? ss->__fp : rt == 30 ? ss->__lr : 0;
		tux_set_tp(v);
	} else {
		fprintf(stderr, "tux: unbekannter Haltepunkt %x bei pc=%llx\n", imm, ss->__pc);
		_exit(127);
	}
	ss->__pc += 4;
}

#define ENTER_ASM \
	".text\n.globl _tux_enter\n_tux_enter:\n" \
	"	mov sp, x1\n	mov x16, x0\n" \
	"	mov x0, xzr\n	mov x1, xzr\n	mov x2, xzr\n	mov x3, xzr\n	mov x4, xzr\n	mov x5, xzr\n" \
	"	mov x6, xzr\n	mov x7, xzr\n	mov x8, xzr\n	mov x9, xzr\n	mov x10, xzr\n	mov x11, xzr\n" \
	"	mov x12, xzr\n	mov x13, xzr\n	mov x14, xzr\n	mov x15, xzr\n	mov x17, xzr\n	mov x18, xzr\n" \
	"	mov x19, xzr\n	mov x20, xzr\n	mov x21, xzr\n	mov x22, xzr\n	mov x23, xzr\n	mov x24, xzr\n" \
	"	mov x25, xzr\n	mov x26, xzr\n	mov x27, xzr\n	mov x28, xzr\n	mov x29, xzr\n	mov x30, xzr\n" \
	"	br x16\n"
#endif

__asm__(ENTER_ASM);
extern void tux_enter(uint64_t entry, uint64_t sp) __attribute__((noreturn));

static void install_trap(void)
{
	struct sigaction sa;
	memset(&sa, 0, sizeof sa);
	sa.sa_sigaction = on_trap;
	// Haltepunkte kommen auch innerhalb eines anderen Handlers vor: nichts sperren
	sa.sa_flags = SA_SIGINFO | SA_NODEFER;
	sigemptyset(&sa.sa_mask);
	sigaction(SIGTRAP, &sa, NULL);
	sigaction(SIGILL, &sa, NULL);
}

// ---------------------------------------------------------------------------------------------
// ELF laden
static size_t align_up(size_t v, size_t a) { return (v + a - 1) & ~(a - 1); }

static int map_elf(const char *hostpath, elfinfo *ei)
{
	int fd = open(hostpath, O_RDONLY);
	if (fd < 0) return -errno;
	Ehdr eh;
	if (pread(fd, &eh, sizeof eh, 0) != sizeof eh || memcmp(eh.ident, "\177ELF", 4) != 0) {
		close(fd);
		return -ENOEXEC;
	}
	if (eh.ident[4] != 2 || eh.ident[5] != 1 || eh.machine != TUX_EM || (eh.type != 2 && eh.type != 3) ||
	    eh.phentsize != sizeof(Phdr)) {
		fprintf(stderr, "tux: kein %s-Linux-Programm (Klasse %d, Maschine %d, Typ %d)\n", TUX_NAME, eh.ident[4],
		    eh.machine, eh.type);
		close(fd);
		return -ENOEXEC;
	}
	Phdr *ph = malloc(eh.phnum * sizeof(Phdr));
	if (pread(fd, ph, eh.phnum * sizeof(Phdr), eh.phoff) != (ssize_t)(eh.phnum * sizeof(Phdr))) die("Programmkopf unlesbar");
	uint64_t lo = ~0ULL, hi = 0, maxalign = tux_page;
	ei->interp[0] = 0;
	ei->phdr = 0;
	for (int i = 0; i < eh.phnum; i++) {
		if (ph[i].type == PT_LOAD) {
			if (ph[i].vaddr < lo) lo = ph[i].vaddr;
			if (ph[i].vaddr + ph[i].memsz > hi) hi = ph[i].vaddr + ph[i].memsz;
			if (ph[i].align > maxalign && ph[i].align <= (1 << 20)) maxalign = ph[i].align;
		} else if (ph[i].type == PT_INTERP && ph[i].filesz < PATH_MAX) {
			if (pread(fd, ei->interp, ph[i].filesz, ph[i].offset) < 0) die("PT_INTERP unlesbar");
		}
	}
	if (lo == ~0ULL) die("%s: keine ladbaren Segmente", hostpath);
	lo &= ~(tux_page - 1);
	hi = align_up(hi, tux_page);
	uint8_t *base;
	if (eh.type == 3) {
		uint8_t *m = mmap(NULL, hi - lo + maxalign, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
		if (m == MAP_FAILED) die("Speicher für %s fehlt: %s", hostpath, strerror(errno));
		base = (uint8_t *)align_up((uintptr_t)m, maxalign) - lo;
		ei->base = (uint64_t)base;
	} else {
		void *m = mmap((void *)lo, hi - lo, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE | MAP_FIXED, -1, 0);
		if (m == MAP_FAILED) {
			fprintf(stderr,
			    "tux: %s will bei Adresse 0x%llx laden (nicht-PIE); Darwin lässt Programmen auf arm64 erst ab 4 GiB Platz.\n",
			    hostpath, (unsigned long long)lo);
			_exit(127);
		}
		base = 0;
		ei->base = 0;
	}
	for (int i = 0; i < eh.phnum; i++) {
		if (ph[i].type == PT_PHDR) ei->phdr = (uint64_t)base + ph[i].vaddr;
		if (ph[i].type != PT_LOAD) continue;
		if (ph[i].filesz && pread(fd, base + ph[i].vaddr, ph[i].filesz, ph[i].offset) != (ssize_t)ph[i].filesz)
			die("%s: Segment unlesbar", hostpath);
		if (!ei->phdr && ph[i].offset <= eh.phoff && eh.phoff < ph[i].offset + ph[i].filesz)
			ei->phdr = (uint64_t)base + ph[i].vaddr + (eh.phoff - ph[i].offset);
		if (ph[i].flags & 1) tux_patch_range(base + ph[i].vaddr, ph[i].filesz);
	}
	// Schutz je Seite: Vereinigung der Segmente, die sie berühren (Seiten sind größer als bei Linux)
	size_t npages = (hi - lo) / tux_page;
	uint8_t *prot = calloc(npages, 1);
	for (int i = 0; i < eh.phnum; i++) {
		if (ph[i].type != PT_LOAD) continue;
		uint64_t s = ph[i].vaddr & ~(tux_page - 1), e = align_up(ph[i].vaddr + ph[i].memsz, tux_page);
		int p = ((ph[i].flags & 4) ? PROT_READ : 0) | ((ph[i].flags & 2) ? PROT_WRITE : 0) | ((ph[i].flags & 1) ? PROT_EXEC : 0);
		for (uint64_t a = s; a < e; a += tux_page) prot[(a - lo) / tux_page] |= p;
	}
	for (size_t i = 0; i < npages; i++) {
		int p = prot[i];
		if (!p) p = PROT_NONE;
		if (mprotect(base + lo + i * tux_page, tux_page, p) != 0 && tux_trace)
			fprintf(stderr, "tux: mprotect %p: %s\n", base + lo + i * tux_page, strerror(errno));
	}
	free(prot);
	ei->entry = (uint64_t)base + eh.entry;
	ei->phnum = eh.phnum;
	ei->phent = eh.phentsize;
	free(ph);
	close(fd);
	return 0;
}

// Stapel nach der System-V-ABI für den Programmstart aufbauen
static uint64_t build_stack(elfinfo *prog, elfinfo *interp, const char *execfn, char **argv, char **envp)
{
	size_t stack_size = 8UL << 20;
	uint8_t *lo = mmap(NULL, stack_size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
	if (lo == MAP_FAILED) die("Stapel: %s", strerror(errno));
	uint8_t *top = lo + stack_size;
	uint8_t *sp = top - 64;
	char *argp[1024], *envpp[4096];
	int argc = 0, envc = 0;
#define PUSH_STR(s, out) do { size_t _n = strlen(s) + 1; sp -= _n; memcpy(sp, s, _n); out = (char *)sp; } while (0)
	char *execfn_s, *platform_s;
	PUSH_STR(execfn, execfn_s);
	PUSH_STR(TUX_NAME, platform_s);
	for (int i = 0; argv[i] && argc < 1023; i++) PUSH_STR(argv[i], argp[argc++]);
	for (int i = 0; envp[i] && envc < 4095; i++) PUSH_STR(envp[i], envpp[envc++]);
	sp -= 16;
	uint8_t *rnd = sp;
	if (getentropy(rnd, 16) != 0) memset(rnd, 0x5a, 16);
	sp = (uint8_t *)((uintptr_t)sp & ~15UL);
	uint64_t aux[40], *a = aux;
#define AUX(t, v) do { *a++ = (t); *a++ = (uint64_t)(v); } while (0)
	AUX(AT_PHDR, prog->phdr);
	AUX(AT_PHENT, prog->phent);
	AUX(AT_PHNUM, prog->phnum);
	AUX(AT_PAGESZ, tux_page);
	AUX(AT_BASE, interp ? interp->base : 0);
	AUX(AT_FLAGS, 0);
	AUX(AT_ENTRY, prog->entry);
	AUX(AT_UID, getuid());
	AUX(AT_EUID, geteuid());
	AUX(AT_GID, getgid());
	AUX(AT_EGID, getegid());
	AUX(AT_PLATFORM, platform_s);
#ifdef TUX_ARM64
	AUX(AT_HWCAP, 0x3);   // FP und ASIMD
#else
	AUX(AT_HWCAP, 0x178bfbff);
#endif
	AUX(AT_HWCAP2, 0);
	AUX(AT_CLKTCK, 100);
	AUX(AT_SECURE, 0);
	AUX(AT_RANDOM, rnd);
	AUX(AT_EXECFN, execfn_s);
	AUX(AT_NULL, 0);
	size_t words = 1 + argc + 1 + envc + 1 + (a - aux);
	sp -= words * 8;
	sp = (uint8_t *)((uintptr_t)sp & ~15UL);
	uint64_t *w = (uint64_t *)sp;
	*w++ = argc;
	for (int i = 0; i < argc; i++) *w++ = (uint64_t)argp[i];
	*w++ = 0;
	for (int i = 0; i < envc; i++) *w++ = (uint64_t)envpp[i];
	*w++ = 0;
	for (uint64_t *p = aux; p < a; p++) *w++ = *p;
	return (uint64_t)sp;
}

// Lädt eine Linux-Datei (Gastpfad) und startet sie; kehrt nur bei Fehlern zurück (negativer Linux-Fehler).
int tux_exec(const char *guest_path, char **argv, char **envp, int depth)
{
	char host[PATH_MAX], canon[PATH_MAX];
	int r = tux_resolve(-100, guest_path, 1, host, canon);
	if (r < 0) return r;
	struct stat st;
	if (stat(host, &st) != 0) return -tux_errno(errno);
	if (S_ISDIR(st.st_mode)) return -21;
	if (access(host, X_OK) != 0 && getuid() != 0) return -13;
	// Skripte mit #!
	char head[256];
	int fd = open(host, O_RDONLY);
	if (fd < 0) return -tux_errno(errno);
	ssize_t n = pread(fd, head, sizeof head - 1, 0);
	close(fd);
	if (n >= 2 && head[0] == '#' && head[1] == '!') {
		if (depth > 4) return -40;
		head[n] = 0;
		char *nl = strchr(head, '\n');
		if (nl) *nl = 0;
		char *interp = head + 2;
		while (*interp == ' ' || *interp == '\t') interp++;
		char *arg = interp;
		while (*arg && *arg != ' ' && *arg != '\t') arg++;
		if (*arg) { *arg++ = 0; while (*arg == ' ' || *arg == '\t') arg++; }
		char *nargv[1030];
		int k = 0;
		nargv[k++] = interp;
		if (*arg) nargv[k++] = arg;
		nargv[k++] = (char *)canon;
		for (int i = 1; argv[i] && k < 1028; i++) nargv[k++] = argv[i];
		nargv[k] = NULL;
		return tux_exec(interp, nargv, envp, depth + 1);
	}
	elfinfo prog, interp;
	memset(&interp, 0, sizeof interp);
	r = map_elf(host, &prog);
	if (r < 0) return r;
	elfinfo *ip = NULL;
	uint64_t entry = prog.entry;
	if (prog.interp[0]) {
		char ihost[PATH_MAX];
		if (tux_resolve(-100, prog.interp, 1, ihost, NULL) < 0) die("Lader %s fehlt in %s", prog.interp, tux_root);
		r = map_elf(ihost, &interp);
		if (r < 0) die("Lader %s lässt sich nicht laden", prog.interp);
		ip = &interp;
		entry = interp.entry;
	}
	strlcpy(tux_exe, canon, sizeof tux_exe);
	uint64_t sp = build_stack(&prog, ip, canon, argv, envp);
	if (tux_trace) fprintf(stderr, "tux: starte %s entry=%llx sp=%llx base=%llx\n", canon, (unsigned long long)entry,
	    (unsigned long long)sp, (unsigned long long)prog.base);
	tux_enter(entry, sp);
}

static void usage(void)
{
	fprintf(stderr,
	    "Aufruf: tux [-r Wurzel] [-C Ordner] [-0 argv0] [-t] Programm [Argumente...]\n"
	    "  Führt ein Linux-Programm (%s-ELF) aus. Wurzel ist /TUX, wenn nichts anderes (-r oder TUX_ROOT) angegeben ist.\n",
	    TUX_NAME);
	_exit(2);
}

extern char **environ;

int main(int argc, char **argv)
{
	tux_page = (size_t)sysconf(_SC_PAGESIZE);
	int i = 1;
	const char *argv0 = NULL, *cwd = NULL;
	if (getenv("TUX_ROOT")) strlcpy(tux_root, getenv("TUX_ROOT"), sizeof tux_root);
	if (getenv("TUX_TRACE")) tux_trace = atoi(getenv("TUX_TRACE"));
	for (; i < argc && argv[i][0] == '-' && argv[i][1]; i++) {
		if (!strcmp(argv[i], "--")) { i++; break; }
		else if (!strcmp(argv[i], "-r") && i + 1 < argc) strlcpy(tux_root, argv[++i], sizeof tux_root);
		else if (!strcmp(argv[i], "-C") && i + 1 < argc) cwd = argv[++i];
		else if (!strcmp(argv[i], "-0") && i + 1 < argc) argv0 = argv[++i];
		else if (!strcmp(argv[i], "-t")) tux_trace = 1;
		else usage();
	}
	if (i >= argc) usage();
	size_t rl = strlen(tux_root);
	while (rl > 1 && tux_root[rl - 1] == '/') tux_root[--rl] = 0;
	// Pfad dieses Programms für execve des Gastes
	{
		char tmp[PATH_MAX];
		uint32_t sz = sizeof tmp;
		extern int _NSGetExecutablePath(char *, uint32_t *);
		if (_NSGetExecutablePath(tmp, &sz) == 0 && realpath(tmp, tux_self)) {}
		else strlcpy(tux_self, argv[0], sizeof tux_self);
	}
	if (pthread_key_create(&tp_key, NULL) != 0) die("pthread_key_create");
	tux_sys_init();
	install_trap();
	tux_signals_init();
	{
		// Arbeitsordner des Aufrufers: unterhalb der Wurzel gilt der Linux-Pfad, bei /tmp, /Users usw. der des Macs
		char hc[PATH_MAX];
		if (getcwd(hc, sizeof hc)) {
			size_t n = strlen(tux_root);
			if (!strncmp(hc, tux_root, n) && (hc[n] == '/' || hc[n] == 0)) strlcpy(tux_cwd, hc[n] ? hc + n : "/", sizeof tux_cwd);
			else if (!strncmp(hc, "/tmp", 4) || !strncmp(hc, "/private/tmp", 12) || !strncmp(hc, "/Users", 6) ||
			    !strncmp(hc, "/Volumes", 8))
				strlcpy(tux_cwd, hc, sizeof tux_cwd);
		}
	}
	if (cwd) strlcpy(tux_cwd, cwd, sizeof tux_cwd);
	char *prog = argv[i];
	char *nargv[1030];
	int k = 0;
	nargv[k++] = (char *)(argv0 ? argv0 : prog);
	for (int j = i + 1; j < argc && k < 1028; j++) nargv[k++] = argv[j];
	nargv[k] = NULL;
	// Gast-Umgebung: wie der Aufrufer, aber PATH nur mit Linux-Ordnern, wenn der Aufrufer keinen gesetzt hat
	// (TUX_* gehört tux)
	char *genv[4096];
	int ne = 0, have_path = 0;
	for (char **e = environ; *e && ne < 4090; e++) {
		if (!strncmp(*e, "TUX_", 4)) continue;
		if (!strncmp(*e, "PATH=", 5)) have_path = 1;
		genv[ne++] = *e;
	}
	(void)have_path;
	genv[ne] = NULL;
	if (!strchr(prog, '/')) {
		// Name ohne Pfad: in den Linux-Ordnern suchen
		static const char *dirs[] = {"/usr/local/bin", "/usr/bin", "/bin", "/usr/local/sbin", "/usr/sbin", "/sbin"};
		static char found[PATH_MAX];
		for (size_t d = 0; d < sizeof dirs / sizeof *dirs; d++) {
			char gp[PATH_MAX], h[PATH_MAX];
			snprintf(gp, sizeof gp, "%s/%s", dirs[d], prog);
			if (tux_resolve(-100, gp, 1, h, NULL) == 0 && access(h, X_OK) == 0) {
				strlcpy(found, gp, sizeof found);
				prog = found;
				break;
			}
		}
	}
	int r = tux_exec(prog, nargv, genv, 0);
	fprintf(stderr, "tux: %s: %s\n", prog, tux_errstr(-r));
	return 127;
}
