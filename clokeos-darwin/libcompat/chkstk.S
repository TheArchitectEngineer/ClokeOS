// ___chkstk_darwin für libclokeos_compat (siehe compat.c).
//
// clang ruft die Funktion mit Apples SDK vor jedem Stackrahmen über 4 KiB auf, auf arm64 unter
// diesem Namen, auf x86_64 unter ____chkstk_darwin (das hat PureDarwins libSystem). Sie tastet
// jede Seite zwischen sp und sp - Größe der Reihe nach an, damit ein großer Rahmen nicht über die
// Schutzseite am Stackende hinwegspringt, und verändert sonst nichts.
//
//   arm64:  x9 = Größe in Bytes (nicht x15 wie __chkstk unter Windows); verändert nur x16, x17
//           und die Flags, wie Apples Fassung in compiler-rt und libpthread
//   x86_64: rax = Größe in Bytes; verändert nur die Flags

	.text
	.globl	___chkstk_darwin
	.p2align 4
___chkstk_darwin:
	.cfi_startproc
#if defined(__arm64__)
	mov	x16, sp
	sub	x17, x16, x9		// x17 = unterstes Byte des neuen Rahmens
1:	sub	x16, x16, #0x1000
	cmp	x16, x17
	b.ls	2f
	ldr	xzr, [x16]		// Seite antasten (Lesen genügt für die Schutzseite)
	b	1b
2:	ldr	xzr, [x17]
	ret
#elif defined(__x86_64__)
	pushq	%rcx
	.cfi_adjust_cfa_offset 8
	pushq	%rax
	.cfi_adjust_cfa_offset 8
	leaq	24(%rsp), %rcx		// rsp des Aufrufers vor dem call
1:	cmpq	$0x1000, %rax
	jb	2f
	subq	$0x1000, %rcx
	testq	%rcx, (%rcx)
	subq	$0x1000, %rax
	jmp	1b
2:	subq	%rax, %rcx
	testq	%rcx, (%rcx)
	popq	%rax
	.cfi_adjust_cfa_offset -8
	popq	%rcx
	.cfi_adjust_cfa_offset -8
	ret
#else
#error Architektur nicht unterstützt
#endif
	.cfi_endproc

	.subsections_via_symbols
