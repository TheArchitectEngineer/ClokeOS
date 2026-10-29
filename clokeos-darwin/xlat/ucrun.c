// ucrun – wie x64run, aber mit Unicorn (QEMU-TCG als Bibliothek) als CPU-Kern statt des eigenen Interpreters.
// Der Gastspeicher liegt in Unicorns eigenem Adressraum (4-KiB-Seiten, jede Gastadresse frei): damit
// entfallen Pagezero, PIE-Verschiebung und Seitengröße des Hosts. Die Systemaufrufe des Gastes
// (BSD-Klasse 2, gleiche Nummern wie arm64) werden an den Host weitergereicht.
//
//   ucrun <x86_64-Mach-O>
#include <mach-o/loader.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <unicorn/unicorn.h>

#define STACK_TOP 0x7ffee0100000ULL
#define STACK_SIZE 0x100000ULL
#pragma clang diagnostic ignored "-Wdeprecated-declarations"

static uc_engine *uc;

static void on_syscall(uc_engine *u, void *ud) {
	uint64_t rax, a[6];
	uc_reg_read(u, UC_X86_REG_RAX, &rax);
	uc_reg_read(u, UC_X86_REG_RDI, &a[0]); uc_reg_read(u, UC_X86_REG_RSI, &a[1]); uc_reg_read(u, UC_X86_REG_RDX, &a[2]);
	if ((rax >> 24) != 2) { fprintf(stderr, "ucrun: kein BSD-Systemaufruf %#llx\n", (unsigned long long)rax); uc_emu_stop(u); return; }
	uint64_t nr = rax & 0xffffff;
	int64_t res;
	if (nr == 1) exit((int)a[0]);
	if (nr == 3 || nr == 4) {  // read/write: Puffer aus dem Gastspeicher holen bzw. zurückschreiben
		size_t n = a[2]; char *b = malloc(n ? n : 1);
		if (nr == 4) { uc_mem_read(u, a[1], b, n); res = syscall(4, a[0], b, n); }
		else { res = syscall(3, a[0], b, n); if (res > 0) uc_mem_write(u, a[1], b, res); }
		free(b);
	} else if (nr == 20 || nr == 24) {  // getpid, getuid: ohne Zeiger
		res = syscall(nr);
	} else { fprintf(stderr, "ucrun: Systemaufruf %llu nicht umgesetzt\n", (unsigned long long)nr); res = -1; }
	uc_reg_write(u, UC_X86_REG_RAX, &res);
}

int main(int argc, char **argv) {
	if (argc < 2) return 2;
	FILE *f = fopen(argv[1], "rb"); if (!f) { perror(argv[1]); return 2; }
	fseek(f, 0, SEEK_END); long fl = ftell(f); fseek(f, 0, SEEK_SET);
	uint8_t *file = malloc(fl); if (fread(file, 1, fl, f) != (size_t)fl) return 2;
	struct mach_header_64 *mh = (void *)file;
	if (mh->magic != MH_MAGIC_64 || mh->cputype != 0x01000007) { fprintf(stderr, "kein x86_64-Mach-O\n"); return 2; }
	uc_err e = uc_open(UC_ARCH_X86, UC_MODE_64, &uc); if (e) { fprintf(stderr, "uc_open: %s\n", uc_strerror(e)); return 3; }
	uint64_t entry = 0;
	struct load_command *lc = (void *)(mh + 1);
	for (uint32_t i = 0; i < mh->ncmds; i++, lc = (void *)((char *)lc + lc->cmdsize)) {
		if (lc->cmd == LC_SEGMENT_64) {
			struct segment_command_64 *s = (void *)lc;
			if (!strcmp(s->segname, "__PAGEZERO") || !s->vmsize) continue;
			uint64_t sz = (s->vmsize + 0xfff) & ~0xfffULL;
			if ((e = uc_mem_map(uc, s->vmaddr, sz, UC_PROT_ALL))) { fprintf(stderr, "map %s: %s\n", s->segname, uc_strerror(e)); return 3; }
			uc_mem_write(uc, s->vmaddr, file + s->fileoff, s->filesize);
		} else if (lc->cmd == LC_UNIXTHREAD) {
			entry = *(uint64_t *)((uint32_t *)((char *)lc + 8) + 2 + 16 * 2);
		}
	}
	uc_mem_map(uc, STACK_TOP - STACK_SIZE, STACK_SIZE, UC_PROT_READ | UC_PROT_WRITE);
	uint64_t sp = STACK_TOP - 0x100; uc_reg_write(uc, UC_X86_REG_RSP, &sp);
	uc_hook h; uc_hook_add(uc, &h, UC_HOOK_INSN, on_syscall, 0, 1, 0, UC_X86_INS_SYSCALL);
	e = uc_emu_start(uc, entry, 0, 0, 0);
	if (e) { fprintf(stderr, "ucrun: %s\n", uc_strerror(e)); return 4; }
	return 0;
}
