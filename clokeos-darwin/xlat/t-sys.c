// Freistehendes x86_64-Testprogramm (kein libc, kein dyld): Systemaufrufe, mmap, Dateien, SSE, Rechenlast.
__asm__(".globl _start\n_start:\n mov %rsp,%rdi\n and $-16,%rsp\n call _cmain\n mov %eax,%edi\n mov $0x2000001,%eax\n syscall\n");
typedef unsigned long u64;
static long sc(long nr, long a, long b, long c, long d, long e, long f) {
	register long r10 __asm__("r10") = d; register long r8 __asm__("r8") = e; register long r9 __asm__("r9") = f;
	long rax = 0x2000000 | nr; unsigned char cf;
	__asm__ volatile("syscall; setc %1" : "+a"(rax), "=r"(cf) : "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
	return cf ? -rax : rax;
}
static void out(const char *s) { long n = 0; while (s[n]) n++; sc(4, 1, (long)s, n, 0, 0, 0); }
static void num(const char *label, long v) {
	char b[64]; int i = 63; b[i] = 0; int neg = v < 0; u64 u = neg ? -v : v;
	do { b[--i] = '0' + u % 10; u /= 10; } while (u);
	if (neg) b[--i] = '-';
	out(label); out(&b[i]); out("\n");
}
static int fail;
#define CHECK(c, m) do { if (!(c)) { out("FEHLER: " m "\n"); fail = 1; } } while (0)
int cmain(long *sp) {
	long argc = sp[0]; char **argv = (char **)(sp + 1);
	num("argc=", argc); out("argv0="); out(argv[0]); out("\n");
	CHECK(argc >= 1, "argc");
	// Umgebung vorhanden?
	char **envp = argv + argc + 1; long ne = 0; while (envp[ne]) ne++; num("envp=", ne > 0);
	// mmap anonym, beschreiben, SSE-Pfad (memset/Summe über double)
	long p = sc(197, 0, 1 << 20, 3, 0x1002, -1, 0);
	CHECK(p > 0, "mmap"); double *d = (double *)p; for (int i = 0; i < 1000; i++) d[i] = i * 0.5; double s = 0; for (int i = 0; i < 1000; i++) s += d[i];
	num("summe*2=", (long)(s * 2));
	CHECK(sc(73, p, 1 << 20, 0, 0, 0, 0) == 0, "munmap");
	// Datei: eigener Kopf lesen, stat
	long fd = sc(5, (long)argv[0], 0, 0, 0, 0, 0); CHECK(fd >= 0, "open");
	unsigned magic = 0; CHECK(sc(3, fd, (long)&magic, 4, 0, 0, 0) == 4, "read"); num("magic=", magic);
	CHECK(magic == 0xfeedfacf, "magic");
	char st[144]; CHECK(sc(339, fd, (long)st, 0, 0, 0, 0) == 0, "fstat64"); num("groesse>0=", *(long *)(st + 96) > 0);
	sc(6, fd, 0, 0, 0, 0, 0);
	CHECK(sc(5, (long)"/gibt/es/nicht", 0, 0, 0, 0, 0) == -2, "ENOENT (CF + errno)");
	CHECK(sc(20, 0, 0, 0, 0, 0, 0) > 0, "getpid");
	// Rechenlast: Sieb des Eratosthenes bis 4 Millionen (Speicher aus mmap), Ergebnis 283146
	long n = 4000000; unsigned char *v = (unsigned char *)sc(197, 0, n, 3, 0x1002, -1, 0);
	for (long i = 2; i * i < n; i++) if (!v[i]) for (long j = i * i; j < n; j += i) v[j] = 1;
	long c = 0; for (long i = 2; i < n; i++) c += !v[i]; num("primes=", c); CHECK(c == 283146, "Sieb");
	out(fail ? "t-sys: FEHLER\n" : "t-sys: ok\n");
	return fail ? 1 : 0;
}
