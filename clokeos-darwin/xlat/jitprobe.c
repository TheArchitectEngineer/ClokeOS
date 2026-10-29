// jitprobe – prüft im Gast, was eine Übersetzungsschicht ohne Kernelpatch braucht:
// Seitengröße, MAP_JIT / RWX, Code erzeugen und ausführen, Schreibfehler auf Code-Seiten abfangen
// (Selbstmodifikation erkennen), Adressraum für 1:1 abgebildeten x86_64-Gastspeicher.
#include <mach/mach.h>
#include <pthread.h>
#include <setjmp.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/sysctl.h>
#include <libkern/OSCacheControl.h>
#include <unistd.h>

static void *fault_page;
static int faults, s_dummy;
static void on_segv(int sig, siginfo_t *si, void *uc) {
	faults++;
	fault_page = si->si_addr;
	// wie ein Übersetzer: Seite wieder beschreibbar machen und das Programm weiterlaufen lassen
	mprotect((void *)((uintptr_t)si->si_addr & ~(uintptr_t)(getpagesize() - 1)), getpagesize(), PROT_READ | PROT_WRITE);
}

static void try_fixed(uint64_t addr, size_t len, const char *what) {
	void *p = mmap((void *)addr, len, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON | MAP_FIXED, -1, 0);
	printf("  mmap FIXED %-26s %#llx len %#zx -> %s\n", what, (unsigned long long)addr, len,
	       p == MAP_FAILED ? "FEHLER" : (p == (void *)addr ? "ok" : "anderswo"));
	if (p != MAP_FAILED) munmap(p, len);
}

int main(void) {
	printf("getpagesize=%d vm_page_size=%lu\n", getpagesize(), (unsigned long)vm_page_size);
	char buf[64]; size_t n;
	const char *keys[] = {"hw.pagesize", "hw.optional.arm64", "hw.optional.armv8_2_sha512", "hw.optional.armv8_1_atomics",
	                      "hw.optional.arm.FEAT_FlagM", "hw.optional.arm.FEAT_FlagM2", "hw.optional.arm.FEAT_LRCPC", "hw.optional.arm.FEAT_LRCPC2",
	                      "hw.optional.floatingpoint", "machdep.cpu.brand_string"};
	for (unsigned i = 0; i < sizeof keys / sizeof *keys; i++) {
		n = sizeof buf; buf[0] = 0;
		long v = 0; size_t vl = sizeof v;
		if (!strcmp(keys[i], "machdep.cpu.brand_string")) {
			printf("  sysctl %-30s %s\n", keys[i], sysctlbyname(keys[i], buf, &n, 0, 0) ? "fehlt" : buf);
		} else {
			int r = sysctlbyname(keys[i], &v, &vl, 0, 0);
			printf("  sysctl %-30s %s", keys[i], r ? "fehlt\n" : ""); if (!r) printf("%ld\n", v);
		}
	}

	puts("JIT:");
	// arm64: mov w0,#42 ; ret
	uint32_t code[] = {0x52800540, 0xd65f03c0};
	void *p = mmap(0, 65536, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANON | MAP_JIT, -1, 0);
	printf("  mmap RWX|MAP_JIT -> %s\n", p == MAP_FAILED ? "FEHLER" : "ok");
	if (p == MAP_FAILED) {
		p = mmap(0, 65536, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
		printf("  mmap RW (ohne JIT) -> %s\n", p == MAP_FAILED ? "FEHLER" : "ok");
	}
	if (p != MAP_FAILED) {
		pthread_jit_write_protect_np(0);
		memcpy(p, code, sizeof code);
		pthread_jit_write_protect_np(1);
		sys_icache_invalidate(p, sizeof code);
		int r = mprotect(p, 65536, PROT_READ | PROT_EXEC);
		printf("  mprotect RX -> %s\n", r ? "FEHLER" : "ok");
		printf("  erzeugte Funktion liefert %d (erwartet 42)\n", ((int (*)(void))p)());
		r = mprotect(p, 65536, PROT_READ | PROT_WRITE | PROT_EXEC);
		printf("  mprotect RWX -> %s\n", r ? "FEHLER" : "ok");
		// Selbstmodifikation: Seite schreibgeschützt, Schreiben löst SIGSEGV/SIGBUS aus
		struct sigaction sa = {.sa_sigaction = on_segv, .sa_flags = SA_SIGINFO | SA_NODEFER};
		sigaction(SIGSEGV, &sa, 0); sigaction(SIGBUS, &sa, 0);
		void *q = mmap(0, 65536, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (q != MAP_FAILED) {
			*(volatile char *)q = 1;
			printf("  Schreibfehler auf RO-Seite abgefangen: %d, Programm läuft weiter (%s)\n", faults, faults == 1 ? "ok" : "?");
		}
	}

	puts("Adressraum (für 1:1 abgebildeten Gastspeicher):");
	printf("  eigenes Programm liegt bei %p (main), Stapel bei %p\n", (void *)main, (void *)&s_dummy);
	try_fixed(0x1000, 0x1000, "Seite nahe 0");
	try_fixed(0x200000000ULL, 0x100000, "8 GiB");
	try_fixed(0x400000000ULL, 0x100000, "16 GiB");
	try_fixed(0x100000000ULL, 0x100000, "x86_64 Image-Basis 4 GiB");
	try_fixed(0x7ffee0000000ULL, 0x100000, "x86_64 Stapel");
	try_fixed(0x7fff00000000ULL, 0x100000, "x86_64 dyld-Gegend");
	try_fixed(0x7ffffff00000ULL, 0x100000, "Obergrenze 47 Bit");
	try_fixed(0x7fffffe00000ULL, 0x1000, "x86_64 Commpage");
	try_fixed(0x400000000000ULL, 0x100000, "64 TiB");
	// höchste Adresse per Hinweis suchen
	uint64_t hi = 0;
	for (int b = 46; b >= 30; b--) {
		void *a = mmap((void *)(1ULL << b), 0x4000, PROT_READ, MAP_PRIVATE | MAP_ANON, -1, 0);
		if (a != MAP_FAILED) { if ((uint64_t)a >= (1ULL << b) && (uint64_t)a > hi) hi = (uint64_t)a; munmap(a, 0x4000); }
	}
	printf("  höchste per Hinweis erhaltene Adresse: %#llx\n", (unsigned long long)hi);
	return 0;
}
