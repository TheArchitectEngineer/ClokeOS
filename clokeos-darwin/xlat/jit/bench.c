// bench.c – freistehende x86_64-Mikro-Benchmarks (ganzzahlig) für den JIT-Vergleich: Rosetta (nativ) / ucrun (Unicorn) / ucrun mit UCRUN_JIT=1
//   Bauen: clang -target x86_64-apple-macos11.0 -O2 -fno-vectorize -fno-slp-vectorize -ffreestanding -fno-builtin -fno-stack-protector -nostdlib -static -Wl,-e,_start
// Ausgabe je Kern: Name, Ergebnis (Prüfsumme), Zeit in ms (gettimeofday im Gast).
__asm__(".globl _start\n_start:\n mov %rsp,%rdi\n and $-16,%rsp\n call _cmain\n mov %eax,%edi\n mov $0x2000001,%eax\n syscall\n");
typedef unsigned long u64; typedef unsigned int u32; typedef long i64;
static long sc(long nr, long a, long b, long c, long d, long e, long f) {
	register long r10 __asm__("r10") = d; register long r8 __asm__("r8") = e; register long r9 __asm__("r9") = f;
	long rax = 0x2000000 | nr; unsigned char cf;
	__asm__ volatile("syscall; setc %1" : "+a"(rax), "=r"(cf) : "D"(a), "S"(b), "d"(c), "r"(r10), "r"(r8), "r"(r9) : "rcx", "r11", "memory");
	return cf ? -rax : rax;
}
static void out(const char *s) { long n = 0; while (s[n]) n++; sc(4, 1, (long)s, n, 0, 0, 0); }
static void numo(u64 u) { char b[32]; int i = 31; b[i] = 0; do { b[--i] = '0' + u % 10; u /= 10; } while (u); out(&b[i]); }
static u64 now_us(void) { long tv[2], tz[2]; sc(116, (long)tv, (long)tz, 0, 0, 0, 0); return (u64)tv[0] * 1000000 + tv[1]; }
static void report(const char *name, u64 res, u64 t0) { out(name); out(" result="); numo(res); out(" ms="); numo((now_us() - t0 + 500) / 1000); out("\n"); }

static unsigned char sv[4000000], big[2000000];
static u64 k_sieve(void) { long n = 4000000; for (long i = 0; i < n; i++) sv[i] = 0; for (int rep = 0; rep < 10; rep++) { for (long i = 0; i < n; i++) sv[i] = 0; for (long i = 2; i * i < n; i++) if (!sv[i]) for (long j = i * i; j < n; j += i) sv[j] = 1; } long c = 0; for (long i = 2; i < n; i++) c += !sv[i]; return c; }
static int fib(int n) { return n < 2 ? n : fib(n - 1) + fib(n - 2); }
static u64 k_fib(void) { return fib(35); }
static u64 k_collatz(void) { u64 best = 0, bi = 0; for (u64 n = 1; n < 1000000; n++) { u64 x = n, s = 0; while (x != 1) { x = (x & 1) ? 3 * x + 1 : x >> 1; s++; } if (s > best) { best = s; bi = n; } } return bi * 1000 + best; }
static u32 crc_tab_free(const unsigned char *p, long n) { u32 c = 0xffffffffu; for (long i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xedb88320u & (0 - (c & 1))); } return ~c; }
static u64 k_crc(void) { for (long i = 0; i < 2000000; i++) big[i] = (unsigned char)(i * 7 + (i >> 8)); return crc_tab_free(big, 2000000); }
static int ma[200][200], mb[200][200], mc[200][200];
static u64 k_matmul(void) { for (int i = 0; i < 200; i++) for (int j = 0; j < 200; j++) { ma[i][j] = i + j; mb[i][j] = i - j; } for (int i = 0; i < 200; i++) for (int j = 0; j < 200; j++) { int s = 0; for (int k = 0; k < 200; k++) s += ma[i][k] * mb[k][j]; mc[i][j] = s; } u64 t = 0; for (int i = 0; i < 200; i++) for (int j = 0; j < 200; j++) t += (u64)(unsigned)mc[i][j]; return t; }
static u64 k_copy(void) { static unsigned char a[1 << 20], b[1 << 20]; for (long i = 0; i < (1 << 20); i++) a[i] = (unsigned char)i; for (int r = 0; r < 32; r++) for (long i = 0; i < (1 << 20); i++) b[i] = a[i] + (unsigned char)r; u64 s = 0; for (long i = 0; i < (1 << 20); i += 8) s += b[i]; return s; }
static u64 k_divmod(void) { u64 s = 0; for (u32 i = 1; i < 6000000; i++) { s += (u64)(1000003u % i) + (1000003u / i); } return s; }
int cmain(long *sp) {
	struct { const char *n; u64 (*f)(void); } k[] = {{"sieve", k_sieve}, {"fib", k_fib}, {"collatz", k_collatz}, {"crc", k_crc}, {"matmul", k_matmul}, {"copy", k_copy}, {"divmod", k_divmod}};
	char **argv = (char **)(sp + 1); long argc = sp[0];
	for (unsigned i = 0; i < sizeof k / sizeof *k; i++) {
		if (argc > 1) { const char *a = argv[1], *b = k[i].n; while (*a && *a == *b) a++, b++; if (*a || *b) continue; }
		u64 t0 = now_us(); u64 r = k[i].f(); report(k[i].n, r, t0);
	}
	return 0;
}
