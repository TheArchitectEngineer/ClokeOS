// Freistehendes arm64-Testprogramm (kein libc, kein dyld) für a64run: Systemaufrufe (svc #0x80, x16 = Nummer, Fehler im Carry-Flag),
// mmap, Dateien, Gleitkomma/NEON, Rechenlast. Gegenstück zu t-sys.c (x86_64).
__asm__(".globl _start\n_start:\n mov x0, sp\n and sp, x0, #-16\n bl _cmain\n mov x16, #1\n svc #0x80\n");
typedef unsigned long u64;
static long sc(long nr, long a, long b, long c, long d, long e, long f) {
	register long x0 __asm__("x0") = a; register long x1 __asm__("x1") = b; register long x2 __asm__("x2") = c;
	register long x3 __asm__("x3") = d; register long x4 __asm__("x4") = e; register long x5 __asm__("x5") = f;
	register long x16 __asm__("x16") = nr; unsigned long cf;
	__asm__ volatile("svc #0x80\n cset %0, cs" : "=r"(cf), "+r"(x0) : "r"(x1), "r"(x2), "r"(x3), "r"(x4), "r"(x5), "r"(x16) : "memory", "cc");
	return cf ? -x0 : x0;
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
	char **envp = argv + argc + 1; long ne = 0; while (envp[ne]) ne++; num("envp=", ne > 0);
	long p = sc(197, 0, 1 << 20, 3, 0x1002, -1, 0);
	CHECK(p > 0, "mmap"); CHECK((p & 0x3fff) == 0, "mmap 16K-ausgerichtet");
	double *d = (double *)p; for (int i = 0; i < 1000; i++) d[i] = i * 0.5; double s = 0; for (int i = 0; i < 1000; i++) s += d[i];
	num("summe*2=", (long)(s * 2));
	CHECK(sc(73, p, 1 << 20, 0, 0, 0, 0) == 0, "munmap");
	long fd = sc(5, (long)argv[0], 0, 0, 0, 0, 0); CHECK(fd >= 0, "open");
	unsigned magic = 0; CHECK(sc(3, fd, (long)&magic, 4, 0, 0, 0) == 4, "read"); num("magic=", magic);
	CHECK(magic == 0xfeedfacf, "magic");
	char st[144]; CHECK(sc(339, fd, (long)st, 0, 0, 0, 0) == 0, "fstat64"); num("groesse>0=", *(long *)(st + 96) > 0);
	sc(6, fd, 0, 0, 0, 0, 0);
	CHECK(sc(5, (long)"/gibt/es/nicht", 0, 0, 0, 0, 0) == -2, "ENOENT (CF + errno)");
	CHECK(sc(20, 0, 0, 0, 0, 0, 0) > 0, "getpid");
	// Rechenlast: Sieb bis 4 Millionen, Ergebnis 283146
	long n = 4000000; unsigned char *v = (unsigned char *)sc(197, 0, n, 3, 0x1002, -1, 0);
	for (long i = 2; i * i < n; i++) if (!v[i]) for (long j = i * i; j < n; j += i) v[j] = 1;
	long c = 0; for (long i = 2; i < n; i++) c += !v[i]; num("primes=", c); CHECK(c == 283146, "Sieb");
	out(fail ? "a64-sys: FEHLER\n" : "a64-sys: ok\n");
	return fail ? 1 : 0;
}
