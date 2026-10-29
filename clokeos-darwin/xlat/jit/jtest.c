// jtest.c – Differenztest des JIT gegen Unicorn: zufällige Befehlsfolgen des unterstützten Teilsatzes, Zustand danach vergleichen
//   jtest [Anzahl] [Seed] [-v]       (Host-Mac, arm64)
// Vergleich: 16 Register, rip, Flags CF/ZF/SF/OF (PF/AF nicht: siehe Studie), Datenbereich. Läuft erst Unicorn (Orakel), dann der JIT
// (mit Rückfall auf Unicorn für nicht unterstützte Befehle, wie in ucrun).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include "jit.h"
#include "ucsync.h"

#define CODE 0x10000000ULL
#define CODE_N 0x100000ULL
#define DATA 0x20000000ULL
#define DATA_N 0x10000ULL
#define SLOT 0x100
#define NSLOT 4096

static uint64_t rs = 88172645463325252ULL;
static uint64_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 7; rs ^= rs << 17; return rs; }
static int rn(int n) { return (int)(rnd() % (uint64_t)n); }

typedef struct { uint8_t b[96]; int n; uint32_t ptr, idx; uint64_t base; uint32_t undef; int wf; } Buf;
static void put8(Buf *B, int v) { B->b[B->n++] = (uint8_t)v; }
static void put32(Buf *B, int64_t v) { int32_t x = (int32_t)v; memcpy(B->b + B->n, &x, 4); B->n += 4; }
static int64_t rimm(int bytes) {
	static const int64_t pool[] = {0, 1, -1, 2, 0x7f, -0x80, 0x80, 0xff, 0x7fff, -0x8000, 0x7fffffff, -0x7fffffff - 1, 31, 32, 63, 64};
	int64_t v = rn(3) ? pool[rn(16)] : (int64_t)rnd();
	if (bytes == 1) v = (int8_t)v; else if (bytes == 2) v = (int16_t)v; else if (bytes == 4) v = (int32_t)v;
	return v;
}
// Befehl mit ModRM: pfx66, rex.w, opcode (1-2 Bytes), reg-Feld (oder /Ziffer), mem (0: Register rm), imm-Bytes
static void emit_rm(Buf *B, int p66, int w, const uint8_t *op, int nop, int reg, int mem, int rmreg, int bytereg, int immb, int64_t imm) {
	int base = 0, index = -1, scale = 0, mod = 3, rip = 0, disp = 0, dispb = 0;
	if (mem) {
		int kind = rn(8);
		if (kind == 0) { rip = 1; mod = 0; dispb = 4; }
		else {
			base = rn(16); mod = rn(3); if (rn(4) == 0) index = rn(16); if (index == 4) index = -1; scale = rn(4);
			if ((base & 7) == 5 && mod == 0) mod = 1;
			disp = mod == 1 ? (int8_t)(rn(64) - 32) : mod == 2 ? rn(512) - 256 : 0; dispb = mod == 1 ? 1 : mod == 2 ? 4 : 0;
			B->ptr |= 1u << base; if (index >= 0) B->idx |= 1u << index;
		}
	}
	int rex = 0; if (w) rex |= 8; if (reg & 8) rex |= 4; if (index >= 0 && mem) rex |= 2;
	if (mem ? (!rip && (base & 8)) : (rmreg & 8)) rex |= 1;
	if (bytereg && ((reg & 7) >= 4 || (!mem && (rmreg & 7) >= 4)) && rn(2)) rex |= 0x40;   // spl/bpl/sil/dil statt ah..bh
	if (bytereg && (reg >= 4 || (!mem && rmreg >= 4)) && !rex) rex |= 0x40;               // Register >= 4 ohne REX wären ah..bh -> das ist erlaubt; hier zufällig REX
	if (p66) put8(B, 0x66);
	if (rex) put8(B, 0x40 | rex);
	for (int i = 0; i < nop; i++) put8(B, op[i]);
	if (!mem) put8(B, 0xc0 | (reg & 7) << 3 | (rmreg & 7));
	else if (rip) {
		put8(B, (reg & 7) << 3 | 5);
		uint64_t next = CODE + (uint64_t)B->base * SLOT + B->n + 4 + immb;
		put32(B, (int64_t)(DATA + 0x4100 + rn(0x100) - next));
	} else if (index >= 0 || (base & 7) == 4) {
		put8(B, mod << 6 | (reg & 7) << 3 | 4); put8(B, scale << 6 | ((index >= 0 ? index : 4) & 7) << 3 | (base & 7));
		if (dispb == 1) put8(B, disp); else if (dispb == 4) put32(B, disp);
	} else {
		put8(B, mod << 6 | (reg & 7) << 3 | (base & 7));
		if (dispb == 1) put8(B, disp); else if (dispb == 4) put32(B, disp);
	}
	if (immb == 1) put8(B, (int)imm); else if (immb == 2) { put8(B, (int)imm); put8(B, (int)(imm >> 8)); } else if (immb == 4) put32(B, imm);
	else if (immb == 8) { memcpy(B->b + B->n, &imm, 8); B->n += 8; }
}
static uint32_t cur_undef;
static int cc_ok(int cc) { static const uint32_t rm[16] = {0x800, 0x800, 1, 1, 0x40, 0x40, 0x41, 0x41, 0x80, 0x80, 0, 0, 0x880, 0x880, 0x8c0, 0x8c0}; return !(rm[cc] & cur_undef); }
static int pick_cc(void) { for (int k = 0; k < 64; k++) { int cc = rn(16); if (cc != 10 && cc != 11 && cc_ok(cc)) return cc; } return -1; }
static int rsz(void) { static const int s[] = {1, 2, 4, 4, 8, 8}; return s[rn(6)]; }
#define CFOF 0x801u
#define ALLF 0x8c1u
static void gen_one(Buf *B) {
	int sz = rsz(), w = sz == 8, p66 = sz == 2, reg = rn(16), rm = rn(16), mem = rn(2), alu = rn(8);
	uint8_t op[3];
	switch (rn(30)) {
	case 0: case 1: case 2: case 3: {  // ALU r/m,r  /  r,r/m
		op[0] = alu * 8 + (rn(2) ? 2 : 0) + (sz > 1); emit_rm(B, p66, w, op, 1, reg, mem, rm, sz == 1, 0, 0); B->wf = 1; B->undef = 0; break; }
	case 4: case 5: {  // ALU imm
		int k = rn(3);
		if (k == 0 && sz == 1) { op[0] = alu * 8 + 4; put8(B, op[0]); put8(B, (int)rimm(1)); }
		else if (k == 0) { if (p66) put8(B, 0x66); if (w) put8(B, 0x48); put8(B, alu * 8 + 5); int ib = sz == 2 ? 2 : 4; int64_t v = rimm(ib); if (ib == 2) { put8(B, (int)v); put8(B, (int)(v >> 8)); } else put32(B, v); }
		else if (sz == 1) { op[0] = 0x80; emit_rm(B, 0, 0, op, 1, alu, mem, rm, 1, 1, rimm(1)); }
		else if (k == 1) { op[0] = 0x83; emit_rm(B, p66, w, op, 1, alu, mem, rm, 0, 1, rimm(1)); }
		else { op[0] = 0x81; int ib = sz == 2 ? 2 : 4; emit_rm(B, p66, w, op, 1, alu, mem, rm, 0, ib, rimm(ib)); }
		B->wf = 1; B->undef = 0; break; }
	case 6: {  // test
		if (rn(2)) { op[0] = 0x84 + (sz > 1); emit_rm(B, p66, w, op, 1, reg, mem, rm, sz == 1, 0, 0); }
		else { op[0] = sz == 1 ? 0xf6 : 0xf7; int ib = sz == 1 ? 1 : sz == 2 ? 2 : 4; emit_rm(B, p66, w, op, 1, 0, mem, rm, sz == 1, ib, rimm(ib)); }
		B->wf = 1; B->undef = 0; break; }
	case 7: {  // inc dec
		op[0] = sz == 1 ? 0xfe : 0xff; emit_rm(B, p66, w, op, 1, rn(2), mem, rm, sz == 1, 0, 0); B->wf = 2; break; }
	case 8: {  // not neg
		op[0] = sz == 1 ? 0xf6 : 0xf7; int k = 2 + rn(2); emit_rm(B, p66, w, op, 1, k, mem, rm, sz == 1, 0, 0); if (k == 3) { B->wf = 1; B->undef = 0; } break; }
	case 9: {  // mul imul div idiv (1 Operand)
		op[0] = sz == 1 ? 0xf6 : 0xf7; int k = 4 + rn(4);
		emit_rm(B, p66, w, op, 1, k, mem, rm, sz == 1, 0, 0);
		B->wf = 1; B->undef = k < 6 ? (0x80 | 0x40 | 0x04 | 0x10) : ALLF; break; }
	case 10: {  // imul 2/3 Operanden
		if (sz == 1) sz = 4, p66 = 0, w = 0;
		int k = rn(3);
		if (k == 0) { op[0] = 0x0f; op[1] = 0xaf; emit_rm(B, p66, w, op, 2, reg, mem, rm, 0, 0, 0); }
		else if (k == 1) { op[0] = 0x6b; emit_rm(B, p66, w, op, 1, reg, mem, rm, 0, 1, rimm(1)); }
		else { op[0] = 0x69; int ib = sz == 2 ? 2 : 4; emit_rm(B, p66, w, op, 1, reg, mem, rm, 0, ib, rimm(ib)); }
		B->wf = 1; B->undef = 0xc4 | 0x10; break; }
	case 11: case 12: {  // Schiebebefehle
		int k = rn(10) < 8 ? (int[]){4, 5, 7}[rn(3)] : rn(8), form = rn(3);
		if (form == 0) { op[0] = sz == 1 ? 0xc0 : 0xc1; int c = rn(sz == 8 ? 64 : 32); emit_rm(B, p66, w, op, 1, k, mem, rm, sz == 1, 1, c); B->undef = c == 1 ? 0x10 : (CFOF | 0x10) & ~1u | 0x800 | 0x10; if (c != 1) B->undef = 0x800 | 0x10; if (c == 0) B->undef = ALLF; }
		else if (form == 1) { op[0] = sz == 1 ? 0xd0 : 0xd1; emit_rm(B, p66, w, op, 1, k, mem, rm, sz == 1, 0, 0); B->undef = 0x10; }
		else { op[0] = sz == 1 ? 0xd2 : 0xd3; emit_rm(B, p66, w, op, 1, k, mem, rm, sz == 1, 0, 0); B->undef = ALLF; }
		if (k >= 0 && k <= 3) B->undef = ALLF;
		B->wf = 1; break; }
	case 13: case 14: case 15: {  // mov
		int k = rn(5);
		if (k == 0) { op[0] = 0x88 + (sz > 1); emit_rm(B, p66, w, op, 1, reg, mem, rm, sz == 1, 0, 0); }
		else if (k == 1) { op[0] = 0x8a + (sz > 1); emit_rm(B, p66, w, op, 1, reg, mem, rm, sz == 1, 0, 0); }
		else if (k == 2) { op[0] = sz == 1 ? 0xc6 : 0xc7; int ib = sz == 1 ? 1 : sz == 2 ? 2 : 4; emit_rm(B, p66, w, op, 1, 0, mem, rm, sz == 1, ib, rimm(ib)); }
		else if (sz == 1) { int r = rn(16); if (r >= 8 || rn(2)) put8(B, 0x40 | (r >> 3)); else r = rn(4); put8(B, 0xb0 + (r & 7)); put8(B, (int)rimm(1)); }
		else { int r = rn(16); if (p66) put8(B, 0x66); if (w || r >= 8) put8(B, 0x40 | (w ? 8 : 0) | (r >> 3)); put8(B, 0xb8 + (r & 7));
			if (w) { int64_t v = rimm(8); memcpy(B->b + B->n, &v, 8); B->n += 8; } else if (p66) { int64_t v = rimm(2); put8(B, (int)v); put8(B, (int)(v >> 8)); } else put32(B, rimm(4)); }
		break; }
	case 16: { op[0] = 0x8d; if (sz == 1) sz = 4, p66 = 0, w = 0; if (sz == 2) { p66 = 0; w = 0; } emit_rm(B, p66, w, op, 1, reg, 1, rm, 0, 0, 0); break; }
	case 17: {  // movzx / movsx
		int k = rn(4); op[0] = 0x0f; op[1] = k == 0 ? 0xb6 : k == 1 ? 0xb7 : k == 2 ? 0xbe : 0xbf; if (sz == 1) sz = 4, p66 = 0, w = 0;
		emit_rm(B, p66, w, op, 2, reg, mem, rm, (k & 1) == 0, 0, 0); break; }
	case 18: { op[0] = 0x63; emit_rm(B, 0, 1, op, 1, reg, mem, rm, 0, 0, 0); break; }
	case 19: { op[0] = 0x86 + (sz > 1); mem = 0; /* xchg mit Speicher: Unicorn rechnet bei unausgerichteten Adressen falsch (atomarer Pfad) */ emit_rm(B, p66, w, op, 1, reg, mem, rm, sz == 1, 0, 0); break; }
	case 20: { int k = rn(4); if (k == 0) { put8(B, 0x99); } else if (k == 1) { put8(B, 0x48); put8(B, 0x99); } else if (k == 2) put8(B, 0x98); else { put8(B, 0x48); put8(B, 0x98); } break; }
	case 21: { int cc = pick_cc(); if (cc < 0) { put8(B, 0x90); break; } op[0] = 0x0f; op[1] = 0x90 + cc; emit_rm(B, 0, 0, op, 2, 0, mem, rm, 1, 0, 0); break; }
	case 22: { int cc = pick_cc(); if (cc < 0) { put8(B, 0x90); break; } op[0] = 0x0f; op[1] = 0x40 + cc; if (sz == 1) sz = 4, p66 = 0, w = 0; emit_rm(B, p66, w, op, 2, reg, mem, rm, 0, 0, 0); break; }
	case 23: { int r = rn(16); if (sz < 4) sz = 4; if (r >= 8 || sz == 8) put8(B, 0x40 | (sz == 8 ? 8 : 0) | (r >> 3)); put8(B, 0x0f); put8(B, 0xc8 + (r & 7)); break; }
	case 24: {  // push/pop Paar
		int r = rn(16), r2 = rn(16); if (r != 4) { if (r >= 8) put8(B, 0x41); put8(B, 0x50 + (r & 7)); } else put8(B, 0x90);
		if (r2 != 4) { if (r2 >= 8) put8(B, 0x41); put8(B, 0x58 + (r2 & 7)); } break; }
	case 25: {  // push imm / push r/m / leave
		int k = rn(4);
		if (k == 0) { put8(B, 0x68); put32(B, rimm(4)); } else if (k == 1) { put8(B, 0x6a); put8(B, (int)rimm(1)); }
		else if (k == 2) { op[0] = 0xff; emit_rm(B, 0, 0, op, 1, 6, 1, rm, 0, 0, 0); } else { put8(B, 0xc9); B->ptr |= 1u << 5; }
		break; }
	case 26: {  // Zeichenketten
		int k = rn(4); B->ptr |= (1u << 6) | (1u << 7); if (rn(3)) put8(B, 0xf3);
		if (k == 0) put8(B, 0xa4); else if (k == 1) { put8(B, 0x48); put8(B, 0xa5); } else if (k == 2) put8(B, 0xaa); else { put8(B, 0x48); put8(B, 0xab); }
		B->idx |= 1u << 1; break; }
	case 27: { op[0] = 0x0f; op[1] = 0x1f; emit_rm(B, 0, 0, op, 2, 0, 1, 0, 0, 0, 0); break; }
	case 28: { // xchg eax,r / nop / x87-Befehl (Fallback) / SSE (Fallback)
		int k = rn(4);
		if (k == 0) { int r = 1 + rn(7); if (r == 4) r = 3; put8(B, 0x90 + r); }
		else if (k == 1) { put8(B, 0x66); put8(B, 0x90); }
		else if (k == 2) { op[0] = 0x0f; op[1] = 0xa2; put8(B, 0x0f); put8(B, 0xa2); B->undef = 0; }   // cpuid -> Unicorn
		else { put8(B, 0xfc); }  // cld
		break; }
	case 29: { op[0] = 0x0f; op[1] = 0xaf; emit_rm(B, p66, w, op, 2, reg, mem, rm, 0, 0, 0); B->wf = 1; B->undef = 0xc4 | 0x10; if (sz == 1 || sz == 2) { B->n = B->n; } break; }
	}
}
static int gen_seq(Buf *B, uint64_t slot) {
	memset(B, 0, sizeof *B); B->base = slot;
	int n = 1 + rn(4); uint32_t um = 0; int have = 0; cur_undef = 0;
	for (int i = 0; i < n; i++) {
		if (B->n > 60) break;
		if (rn(6) == 0 && i > 0) {  // jcc über einen Befehl
			int cc = pick_cc(); if (cc < 0) { put8(B, 0x90); continue; } put8(B, 0x70 + cc); put8(B, 3); put8(B, 0x83); put8(B, 0xc0 + rn(2) * 0); put8(B, 0x01 * 0 + 1);
			B->undef = 0; B->wf = 0; continue; }
		B->wf = 0; B->undef = 0; gen_one(B);
		if (B->wf == 1) { um = B->undef; cur_undef = um; have = 1; } else if (B->wf == 2) { /* inc/dec: CF bleibt */ }
	}
	put8(B, 0x0f); put8(B, 0x05);
	return (int)um;
}
// "add eax,1" im jcc-Rumpf verändert Flags auf beiden Seiten gleich

static uc_engine *uc; static uint8_t *code_h, *data_h;
static uint8_t code_init[CODE_N], data_init[DATA_N], data_jit[DATA_N], data_uc[DATA_N];
static uint64_t pool64(void) {
	static const uint64_t p[] = {0, 1, ~0ULL, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x7fffffffULL, 0x80000000ULL, 0xffffffffULL, 0x7fffffffffffffffULL, 0x8000000000000000ULL, 2, 3, 31, 63};
	return rn(3) ? p[rn(19)] : rn(2) ? rnd() : (uint64_t)rn(1000);
}

int main(int argc, char **argv) {
	long N = argc > 1 ? atol(argv[1]) : 20000; if (argc > 2) rs ^= (uint64_t)atoll(argv[2]) * 0x9E3779B97F4A7C15ULL; int verbose = argc > 3;
	JitCtx c; if (jit_init(&c)) { fprintf(stderr, "jit_init fehlgeschlagen\n"); return 2; }
	code_h = jit_guest_alloc(CODE, CODE_N); data_h = jit_guest_alloc(DATA, DATA_N);
	if (!code_h || !data_h) { fprintf(stderr, "jit_guest_alloc fehlgeschlagen\n"); return 2; }
	c.pend = (volatile uint32_t *)calloc(1, 4);
	if (uc_open(UC_ARCH_X86, UC_MODE_64, &uc)) return 2;
	uc_mem_map_ptr(uc, CODE, CODE_N, UC_PROT_ALL, code_h); uc_mem_map_ptr(uc, DATA, DATA_N, UC_PROT_ALL, data_h);
	setvbuf(stdout, NULL, _IOLBF, 0);
	long fails = 0, skipped = 0, fbtests = 0, ok = 0, pfdiff = 0;
	for (long it = 0; it < N; it++) {
		int slot = it % NSLOT; if (getenv("JT_TRACE")) printf("it %ld\n", it);
		if (slot == 0 && it) { jit_flush(); uc_ctl_flush_tb(uc); }
		Buf B; int um = gen_seq(&B, slot); uint64_t pc = CODE + (uint64_t)slot * SLOT; uint64_t end = pc + B.n - 2;
		memcpy(code_h + slot * SLOT, B.b, B.n);
		if (getenv("JT_TRACE")) { printf("  code:"); for (int q = 0; q < B.n; q++) printf(" %02x", B.b[q]); printf("\n"); }
		// Anfangszustand
		uint64_t r0[16], ef0 = 2 | (rn(2) ? 1 : 0) | (rn(2) ? 4 : 0) | (rn(2) ? 0x10 : 0) | (rn(2) ? 0x40 : 0) | (rn(2) ? 0x80 : 0) | (rn(2) ? 0x800 : 0);
		for (int i = 0; i < 16; i++) r0[i] = pool64();
		for (int i = 0; i < 16; i++) if (B.ptr & (1u << i)) r0[i] = DATA + 0x4000 + (rnd() % 0x100);
		for (int i = 0; i < 16; i++) if (B.idx & (1u << i)) r0[i] = rn(8);
		r0[4] = DATA + 0x8000 + (rn(4) * 8); if (B.ptr & (1u << 4)) r0[4] = DATA + 0x8000;
		if (B.ptr & (1u << 6)) r0[6] = DATA + 0x2000 + rn(64);
		if (B.ptr & (1u << 7)) r0[7] = DATA + 0x3000 + rn(64);
		if (B.idx & 2) r0[1] = rn(20);
		for (size_t i = 0; i < DATA_N; i += 8) { uint64_t v = rn(5) ? rnd() : 0; if (i >= 0x3f00 && i < 0x4200 && rn(2)) v = 0; memcpy(data_h + i, &v, 8); }
		memcpy(data_init, data_h, DATA_N); memcpy(code_init, code_h, CODE_N);
		// 1. Unicorn allein
		for (int i = 0; i < 16; i++) uc_reg_write(uc, jit_ucreg[i], &r0[i]);
		uc_reg_write(uc, UC_X86_REG_EFLAGS, &ef0);
		uc_err e = uc_emu_start(uc, pc, end, 0, 0);
		if (e) { skipped++; if (verbose) printf("skip %ld: %s\n", it, uc_strerror(e)); continue; }
		{ uint64_t rp; uc_reg_read(uc, UC_X86_REG_RIP, &rp); if (rp != end) { skipped++; continue; } }
		if (memcmp(code_h, code_init, CODE_N)) { skipped++; memcpy(code_h, code_init, CODE_N); continue; }  // Befehl hat den eigenen Code überschrieben (SMC wird nicht verglichen)
		uint64_t ru[16], efu; for (int i = 0; i < 16; i++) uc_reg_read(uc, jit_ucreg[i], &ru[i]);
		uc_reg_read(uc, UC_X86_REG_EFLAGS, &efu); memcpy(data_uc, data_h, DATA_N);
		if (getenv("JT_ONLY") && atol(getenv("JT_ONLY")) == it) setenv("UCRUN_JIT_TRACE", "2", 1);
		// 2. JIT (mit Rückfall)
		memcpy(data_h, data_init, DATA_N);
		memcpy(c.r, r0, sizeof r0); c.rip = pc; jit_set_eflags(&c, ef0); int fb = 0, bad = 0;
		for (int guard = 0; guard < 200; guard++) {
			int st = jit_run(&c);
			if (st == JIT_SYSCALL) break;
			if (st == JIT_FALLBACK || st == JIT_FAULT) {
				fb++; uc_err e2 = jit_uc_fallback(uc, &c); if (e2) { bad = 2; break; }
				if (c.rip == end) { st = JIT_SYSCALL; break; } continue;
			}
			bad = 1; break;
		}
		if (bad == 2) { skipped++; continue; }
		if (bad) { fails++; printf("FEHLER %ld: unerwartetes Ereignis %d\n", it, (int)c.status); continue; }
		if (fb) fbtests++;
		memcpy(data_jit, data_h, DATA_N);
		uint64_t efj = jit_eflags_of(&c); uint64_t mask = ALLF & ~(uint64_t)um;
		int same = 1; char why[200] = "";
		for (int i = 0; i < 16; i++) if (c.r[i] != ru[i]) { same = 0; snprintf(why + strlen(why), 100, " r%d %llx!=%llx", i, (unsigned long long)c.r[i], (unsigned long long)ru[i]); }
		if ((efj & mask) != (efu & mask)) { same = 0; snprintf(why + strlen(why), 80, " fl %llx!=%llx(mask %llx)", (unsigned long long)(efj & ALLF), (unsigned long long)(efu & ALLF), (unsigned long long)mask); }
		if (c.rip != end + 2 && c.rip != end) { same = 0; snprintf(why + strlen(why), 60, " rip %llx", (unsigned long long)c.rip); }
		if (memcmp(data_jit, data_uc, DATA_N)) { same = 0; size_t k = 0; while (data_jit[k] == data_uc[k]) k++; snprintf(why + strlen(why), 60, " mem@%zx", k); }
		if (!same) {
			fails++;
			if (fails <= 25) { printf("FEHLER %ld:", it); for (int i = 0; i < B.n - 2; i++) printf(" %02x", B.b[i]); printf("  |%s\n  init:", why); for (int i = 0; i < 16; i++) printf(" %llx", (unsigned long long)r0[i]); printf(" ef=%llx\n", (unsigned long long)ef0); }
		} else ok++;
		(void)pfdiff;
	}
	uint64_t b, i, f; jit_stats(&b, &i, &f);
	printf("jtest: %ld Tests, %ld gleich, %ld Fehler, %ld übersprungen (Unicorn-Ausnahme), %ld mit Rückfall; JIT: %llu Blöcke, %llu Befehle, %llu Rückfälle\n", N, ok, fails, skipped, fbtests, (unsigned long long)b, (unsigned long long)i, (unsigned long long)f);
	return fails != 0;
}
