// Längendecoder für x86-64-Befehle, nur um die Grenzen zu finden (tux muss `syscall` von gleich aussehenden
// Bytes in anderen Befehlen unterscheiden). Gibt die Länge zurück, 0 bei ungültigem Befehl.
#ifndef X86DEC_H
#define X86DEC_H
#include <stddef.h>
#include <stdint.h>

// Tabelle der Ein-Byte-Befehle: 1 = ModRM, 2 = imm8, 4 = imm32 (z), 8 = imm16, 16 = imm64/32 (B8..BF), 32 = ungültig,
// 64 = moffs, 128 = F6/F7-Gruppe
static const uint8_t x86_one[256] = {
#define R4(x) x, x, x, x
	// 00-0F
	1,1,1,1,2,4,32,32, 1,1,1,1,2,4,32,0,
	1,1,1,1,2,4,32,32, 1,1,1,1,2,4,32,32,
	1,1,1,1,2,4,0,32, 1,1,1,1,2,4,0,32,
	1,1,1,1,2,4,0,32, 1,1,1,1,2,4,0,32,
	// 40-4F REX (vom Decoder behandelt)
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	0,0,0,0,0,0,0,0, 0,0,0,0,0,0,0,0,
	32,32,32,1,0,0,0,0, 4,1|4,2,1|2,0,0,0,0,
	2,2,2,2,2,2,2,2, 2,2,2,2,2,2,2,2,
	// 80-8F
	1|2,1|4,32,1|2,1,1,1,1, 1,1,1,1,1,1,1,1,
	0,0,0,0,0,0,0,0, 0,0,32,0,0,0,0,0,
	64,64,64,64,0,0,0,0, 2,4,0,0,0,0,0,0,
	2,2,2,2,2,2,2,2, 16,16,16,16,16,16,16,16,
	// C0-CF
	1|2,1|2,8,0,0,0,1|2,1|4, 8|2,0,8,0,0,2,32,0,
	1,1,1,1,32,32,0,0, 1,1,1,1,1,1,1,1,
	2,2,2,2,2,2,2,2, 4,4,32,2,0,0,0,0,
	0,0,0,0,0,0,128,128, 0,0,0,0,0,0,1,1,
};

static const uint8_t x86_prefix[256] = {[0x26]=1,[0x2e]=1,[0x36]=1,[0x3e]=1,[0x64]=1,[0x65]=1,[0x66]=1,[0x67]=1,[0xf0]=1,[0xf2]=1,[0xf3]=1};

static size_t x86_len(const uint8_t *p, size_t avail)
{
	size_t i = 0;
	int opsize16 = 0, addr32 = 0, rexw = 0;
	while (i < avail && i < 15 && x86_prefix[p[i]]) {
		if (p[i] == 0x66) opsize16 = 1;
		if (p[i] == 0x67) addr32 = 1;
		i++;
	}
	if (i >= avail) return 0;
	if ((p[i] & 0xf0) == 0x40) { rexw = p[i] & 8; i++; }
	if (i >= avail) return 0;
	uint8_t op = p[i++];
	int modrm = 0, imm = 0;
	int vex = 0;
	if (op == 0xc5 || op == 0xc4 || op == 0x62) {
		// VEX/EVEX: Nutzlast, dann Befehlsbyte
		int pl = op == 0xc5 ? 1 : op == 0xc4 ? 2 : 3;
		if (i + pl >= avail) return 0;
		int map = op == 0xc5 ? 1 : (p[i] & (op == 0x62 ? 7 : 31));
		i += pl;
		uint8_t o2 = p[i++];
		modrm = 1;
		if (map == 3) imm = 1;
		else if (map == 1 && ((o2 >= 0x70 && o2 <= 0x73) || o2 == 0xc2 || (o2 >= 0xc4 && o2 <= 0xc6))) imm = 1;
		vex = 1;
	} else if (op == 0x0f) {
		if (i >= avail) return 0;
		uint8_t o2 = p[i++];
		if (o2 == 0x38) { i++; modrm = 1; }
		else if (o2 == 0x3a) { i++; modrm = 1; imm = 1; }
		else if (o2 == 0x05 || o2 == 0x0b || (o2 >= 0x30 && o2 <= 0x37) || o2 == 0x77 || o2 == 0xa0 || o2 == 0xa1 ||
		    o2 == 0xa2 || o2 == 0xa8 || o2 == 0xa9 || o2 == 0xaa || (o2 >= 0xc8 && o2 <= 0xcf) || o2 == 0x06 || o2 == 0x07 ||
		    o2 == 0x08 || o2 == 0x09 || o2 == 0x0e) { /* ohne ModRM */ }
		else if (o2 >= 0x80 && o2 <= 0x8f) imm = 4;
		else if (o2 == 0x04 || o2 == 0x0a || o2 == 0x0c) return 0;
		else {
			modrm = 1;
			if ((o2 >= 0x70 && o2 <= 0x73) || o2 == 0xa4 || o2 == 0xac || o2 == 0xba || o2 == 0xc2 ||
			    (o2 >= 0xc4 && o2 <= 0xc6) || o2 == 0x0f) imm = 1;
		}
	} else {
		uint8_t t = x86_one[op];
		if (t & 32) return 0;
		if (t & 1) modrm = 1;
		if (t & 2) imm = 1;
		if (t & 4) imm = opsize16 ? 2 : 4;
		if (t & 8) imm = 2;
		if (t == (8 | 2 | 0) || op == 0xc8) imm = 3;
		if (t & 16) imm = rexw ? 8 : opsize16 ? 2 : 4;
		if (t & 64) imm = addr32 ? 4 : 8;
		if (t & 128) modrm = 1;
		if (op == 0x68) imm = opsize16 ? 2 : 4;
		if (op == 0xe8 || op == 0xe9) imm = 4;
		if (op == 0xa9 || op == 0x05 || (op & 0xc7) == 0x05) imm = opsize16 ? 2 : 4;
		if (op == 0xc2 || op == 0xca) imm = 2;
		(void)vex;
	}
	if (modrm) {
		if (i >= avail) return 0;
		uint8_t m = p[i++];
		int mod = m >> 6, rm = m & 7;
		if (mod != 3 && rm == 4) {
			if (i >= avail) return 0;
			uint8_t sib = p[i++];
			if (mod == 0 && (sib & 7) == 5) i += 4;
		}
		if (mod == 1) i += 1;
		else if (mod == 2) i += 4;
		else if (mod == 0 && rm == 5) i += 4;
		if (op == 0xf6 && ((m >> 3) & 7) < 2) imm = 1;
		if (op == 0xf7 && ((m >> 3) & 7) < 2) imm = opsize16 ? 2 : 4;
	}
	i += imm;
	return i <= avail ? i : 0;
}
#endif
