// x64run – Machbarkeitsbeweis: ein statisches x86_64-Mach-O-Programm auf arm64 ausführen.
//
// Kein Übersetzer, sondern ein winziger Interpreter für etwa 30 Befehle. Er zeigt die Teile, die für
// jede Übersetzungsschicht gleich sind und die ohne Kernelpatch gehen: Mach-O laden (Segmente in
// Gastspeicher legen, Einsprung aus LC_UNIXTHREAD / LC_MAIN), Gastregister in einer Struktur halten,
// BSD-Systemaufrufe des Gastes (Klasse 2, Nummern sind auf Darwin für x86_64 und arm64 gleich) an den
// Host weiterreichen. Gesamte Adressübersetzung: Gastadresse -> Puffer (kein 1:1, siehe docs).
//
//   x64run <x86_64-Mach-O> [Argumente]
#include <mach-o/loader.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define STACK_TOP 0x7ffee0100000ULL
#define STACK_SIZE 0x100000ULL

static uint8_t *img, *stk;
static uint64_t img_base, img_size;
static int trace;

static void die(const char *m, uint64_t a) { fprintf(stderr, "x64run: %s (%#llx)\n", m, (unsigned long long)a); exit(120); }

static uint8_t *G(uint64_t a, uint64_t len) {
	if (a >= img_base && a + len <= img_base + img_size) return img + (a - img_base);
	if (a >= STACK_TOP - STACK_SIZE && a + len <= STACK_TOP) return stk + (a - (STACK_TOP - STACK_SIZE));
	die("Gastzugriff ausserhalb", a);
	return 0;
}
#define RD(T, a) (*(T *)G((a), sizeof(T)))

enum { RAX, RCX, RDX, RBX, RSP, RBP, RSI, RDI, R8, R9, R10, R11, R12, R13, R14, R15 };
static uint64_t r[16], rip;
static int zf, sf, cf, of;

static void flags_logic(uint64_t v, int w) { zf = (w ? v : (uint32_t)v) == 0; sf = (w ? v >> 63 : (uint32_t)v >> 31) & 1; cf = of = 0; }
static uint64_t alu(int op, uint64_t a, uint64_t b, int w) {  // op: 0 add, 5 sub/cmp, 6 xor
	uint64_t v;
	if (!w) { a = (uint32_t)a; b = (uint32_t)b; }
	switch (op) {
	case 0: v = a + b; cf = w ? v < a : (v >> 32) & 1; break;
	case 5: v = a - b; cf = a < b; break;
	default: v = a ^ b; flags_logic(v, w); return w ? v : (uint32_t)v;
	}
	if (!w) v = (uint32_t)v;
	zf = v == 0; sf = (w ? v >> 63 : v >> 31) & 1;
	uint64_t s = w ? 63 : 31;
	of = op == 0 ? (((a ^ v) & (b ^ v)) >> s) & 1 : (((a ^ b) & (a ^ v)) >> s) & 1;
	return v;
}

// ModRM; liefert Registernummer oder Gastadresse
static int rex, rexr, rexb, rexx, rexw;
static int mod_, reg_, rm_;
static uint64_t ea;
static void modrm(uint8_t *p, int *len) {
	uint8_t m = p[0]; int n = 1;
	mod_ = m >> 6; reg_ = ((m >> 3) & 7) | (rexr << 3); rm_ = (m & 7);
	if (mod_ == 3) { rm_ |= rexb << 3; *len = n; return; }
	if (rm_ == 4) {
		uint8_t s = p[n++]; int sc = s >> 6, ix = ((s >> 3) & 7) | (rexx << 3), bs = (s & 7);
		uint64_t base = (bs == 5 && mod_ == 0) ? 0 : r[bs | (rexb << 3)];
		ea = base + (ix == 4 ? 0 : r[ix] << sc);
		if (bs == 5 && mod_ == 0) { ea += (int64_t)*(int32_t *)(p + n); n += 4; }
	} else if (rm_ == 5 && mod_ == 0) {
		n += 4; ea = 0; rm_ = -1;  // rip-relativ: Ziel wird beim Aufrufer aus rip + Länge + disp32 berechnet
	} else ea = r[rm_ | (rexb << 3)];
	if (mod_ == 1) { ea += (int8_t)p[n]; n += 1; }
	else if (mod_ == 2) { ea += (int32_t)*(int32_t *)(p + n); n += 4; }
	*len = n;
}

static int64_t host_syscall(uint64_t nr, uint64_t *a);

int main(int argc, char **argv) {
	if (argc < 2) { fprintf(stderr, "usage: x64run <x86_64 Mach-O>\n"); return 2; }
	trace = getenv("X64RUN_TRACE") != 0;
	FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
	fseek(f, 0, SEEK_END); long fl = ftell(f); fseek(f, 0, SEEK_SET);
	uint8_t *file = malloc(fl); if (fread(file, 1, fl, f) != (size_t)fl) die("lesen", 0);
	struct mach_header_64 *mh = (void *)file;
	if (mh->magic != MH_MAGIC_64 || mh->cputype != 0x01000007) die("kein x86_64-Mach-O (Fat-Dateien nicht unterstützt)", mh->magic);
	// Segmente
	uint64_t lo = ~0ULL, hi = 0, entry = 0, entryoff = 0; int have_main = 0;
	struct load_command *lc = (void *)(mh + 1);
	for (uint32_t i = 0; i < mh->ncmds; i++, lc = (void *)((char *)lc + lc->cmdsize)) {
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64 *s = (void *)lc;
			if (!strcmp(s->segname, "__PAGEZERO")) continue;
			if (s->vmaddr < lo) lo = s->vmaddr;
			if (s->vmaddr + s->vmsize > hi) hi = s->vmaddr + s->vmsize;
		}
	}
	img_base = lo; img_size = hi - lo;
	img = mmap(0, img_size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	stk = mmap(0, STACK_SIZE, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
	lc = (void *)(mh + 1);
	for (uint32_t i = 0; i < mh->ncmds; i++, lc = (void *)((char *)lc + lc->cmdsize)) {
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64 *s = (void *)lc;
			if (!strcmp(s->segname, "__PAGEZERO")) continue;
			memcpy(img + (s->vmaddr - lo), file + s->fileoff, s->filesize);
		} else if (lc->cmd == LC_UNIXTHREAD) {
			uint32_t *t = (uint32_t *)((char *)lc + 8);  // flavor, count, state
			entry = *(uint64_t *)(t + 2 + 16 * 2);       // rip ist Register 16 von x86_THREAD_STATE64
		} else if (lc->cmd == LC_MAIN) {
			entryoff = ((struct entry_point_command *)lc)->entryoff; have_main = 1;
		} else if (lc->cmd == LC_LOAD_DYLIB || lc->cmd == LC_LOAD_DYLINKER) {
			die("dynamisches Programm: braucht dyld + x86_64-libSystem im Gast, siehe docs", lc->cmd);
		}
	}
	if (have_main) entry = lo + entryoff;
	rip = entry; r[RSP] = STACK_TOP - 0x100;
	if (trace) fprintf(stderr, "x64run: Image %#llx..%#llx, Einsprung %#llx\n", (unsigned long long)lo, (unsigned long long)hi, (unsigned long long)rip);

	for (;;) {
		uint8_t *p = G(rip, 16); int k = 0, len, w;
		rex = rexr = rexb = rexx = rexw = 0;
		if ((p[0] & 0xf0) == 0x40) { rex = p[0]; rexw = (rex >> 3) & 1; rexr = (rex >> 2) & 1; rexx = (rex >> 1) & 1; rexb = rex & 1; k = 1; }
		w = rexw; uint8_t op = p[k++];
		if (trace) fprintf(stderr, "  %#llx: op %02x\n", (unsigned long long)rip, op);
#define NEXT(n) do { rip += (n); goto next; } while (0)
#define SETREG(i, v) (r[i] = w ? (v) : (uint32_t)(v))
		if (op >= 0xb8 && op <= 0xbf) {
			int d = (op & 7) | (rexb << 3);
			if (w) { r[d] = *(uint64_t *)(p + k); NEXT(k + 8); }
			r[d] = *(uint32_t *)(p + k); NEXT(k + 4);
		} else if (op >= 0x50 && op <= 0x57) { r[RSP] -= 8; RD(uint64_t, r[RSP]) = r[(op & 7) | (rexb << 3)]; NEXT(k);
		} else if (op >= 0x58 && op <= 0x5f) { r[(op & 7) | (rexb << 3)] = RD(uint64_t, r[RSP]); r[RSP] += 8; NEXT(k);
		} else if (op == 0xe8) { int32_t d = *(int32_t *)(p + k); r[RSP] -= 8; RD(uint64_t, r[RSP]) = rip + k + 4; rip = rip + k + 4 + d; continue;
		} else if (op == 0xc3) { rip = RD(uint64_t, r[RSP]); r[RSP] += 8; continue;
		} else if (op == 0xeb) { rip = rip + k + 1 + (int8_t)p[k]; continue;
		} else if (op == 0x74 || op == 0x75) { int t = op == 0x74 ? zf : !zf; rip = rip + k + 1 + (t ? (int8_t)p[k] : 0); continue;
		} else if (op == 0x0f && p[k] == 0x05) {
			uint64_t a[6] = {r[RDI], r[RSI], r[RDX], r[R10], r[R8], r[R9]};
			if ((r[RAX] >> 24) != 2) die("kein BSD-Systemaufruf (Mach-Falle?)", r[RAX]);
			uint64_t nr = r[RAX] & 0xffffff;
			if (nr == 1) exit((int)a[0]);
			if (nr == 4 || nr == 3) a[1] = (uint64_t)G(a[1], a[2]);  // Zeiger des Gastes in Zeiger des Hosts
			else if (nr == 5) a[0] = (uint64_t)G(a[0], 1);
			int64_t res = host_syscall(nr, a);
			r[RAX] = res; r[RCX] = rip + k + 1; NEXT(k + 1);
		} else if (op == 0x89 || op == 0x8b || op == 0x8d || op == 0x01 || op == 0x29 || op == 0x31 || op == 0x39) {
			modrm(p + k, &len);
			if (rm_ == -1) ea = rip + k + len + (int64_t)*(int32_t *)(p + k + len - 4);
			uint64_t src;
			if (op == 0x8d) { SETREG(reg_, ea); NEXT(k + len); }
			if (op == 0x8b) { src = mod_ == 3 ? r[rm_] : (w ? RD(uint64_t, ea) : RD(uint32_t, ea)); SETREG(reg_, src); NEXT(k + len); }
			if (op == 0x89) { if (mod_ == 3) SETREG(rm_, r[reg_]); else if (w) RD(uint64_t, ea) = r[reg_]; else RD(uint32_t, ea) = r[reg_]; NEXT(k + len); }
			int aop = op == 0x01 ? 0 : (op == 0x29 || op == 0x39) ? 5 : 6;
			uint64_t dst = mod_ == 3 ? r[rm_] : (w ? RD(uint64_t, ea) : RD(uint32_t, ea));
			uint64_t v = alu(aop, dst, r[reg_], w);
			if (op != 0x39) { if (mod_ == 3) SETREG(rm_, v); else if (w) RD(uint64_t, ea) = v; else RD(uint32_t, ea) = v; }
			NEXT(k + len);
		} else if (op == 0x83 || op == 0x81 || op == 0xc7) {
			modrm(p + k, &len);
			if (rm_ == -1) ea = rip + k + len + (int64_t)*(int32_t *)(p + k + len - 4) + (op == 0x83 ? 1 : 4);
			int imm = op == 0x83 ? 1 : 4; int64_t iv = imm == 1 ? (int8_t)p[k + len] : *(int32_t *)(p + k + len);
			if (op == 0xc7) { if (mod_ == 3) SETREG(rm_, (uint64_t)iv); else if (w) RD(uint64_t, ea) = iv; else RD(uint32_t, ea) = iv; NEXT(k + len + imm); }
			int aop = (reg_ & 7) == 0 ? 0 : (reg_ & 7) == 5 || (reg_ & 7) == 7 ? 5 : 6;
			uint64_t dst = mod_ == 3 ? r[rm_] : (w ? RD(uint64_t, ea) : RD(uint32_t, ea));
			uint64_t v = alu(aop, dst, (uint64_t)iv, w);
			if ((reg_ & 7) != 7) { if (mod_ == 3) SETREG(rm_, v); else if (w) RD(uint64_t, ea) = v; else RD(uint32_t, ea) = v; }
			NEXT(k + len + imm);
		}
		fprintf(stderr, "x64run: Befehl %02x %02x bei %#llx nicht umgesetzt\n", op, p[k], (unsigned long long)rip);
		return 121;
	next:;
	}
}

#include <sys/syscall.h>
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
static int64_t host_syscall(uint64_t nr, uint64_t *a) {
	// Darwin: gleiche BSD-Nummern auf x86_64 und arm64, gleiche Strukturen (LP64) -> Durchreichen
	int64_t v = syscall((int)nr, a[0], a[1], a[2], a[3], a[4], a[5]);
	return v;
}
