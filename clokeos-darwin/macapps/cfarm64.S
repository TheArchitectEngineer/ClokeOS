/* arm64-Teil von Darlings Weiterleitung und NSInvocation für ClokeOS (ersetzt dort
 * CFForwardingPrep.S und NSInvoke-x86.S, die es nur für x86 und arm32 gibt).
 *
 *   __CF_forwarding_prep_0/1  objc4s Weiterleitung (objc_setForwardHandler): sichert alle
 *                             Argumentregister und ruft __CFArm64Forwarding (cfarm64.m)
 *   __CF_forwarding_prep_b    dasselbe für Darlings Weiterleitungs-Blöcke (__CFArm64BlockForwarding)
 *   ___CFArm64Call            ruft eine Funktion mit vorbereiteten Registern und Stack-Argumenten
 *                             (für -[NSInvocation invoke], __CFArm64Invoke)
 *
 * Gesicherter Block (cfa64_saved in cfarm64.m), 0xd0 Byte:
 *   0x00 x0-x7   0x40 x8 (Zeiger für große Rückgabewerte)   0x48 Stack-Argumente des Aufrufers
 *   0x50 q0-q7 (je 16 Byte)
 * Rückgabe-Block (cfa64_ret), 0x30 Byte: 0x00 x0, x1   0x10 d0-d3
 *
 * Alle drei haben einen Rahmen über x29 mit CFI, damit Ausnahmen (doesNotRecognizeSelector:,
 * Ausnahmen in der aufgerufenen Methode) durch sie hindurch abgewickelt werden. */

#if defined(__arm64__)

    .text

/* --- Weiterleitung ------------------------------------------------------------------------- */
    .globl  __CF_forwarding_prep_0
    .globl  __CF_forwarding_prep_1
    .p2align 2
__CF_forwarding_prep_0:
__CF_forwarding_prep_1:
    .cfi_startproc
    stp     x29, x30, [sp, #-16]!
    .cfi_def_cfa_offset 16
    mov     x29, sp
    .cfi_def_cfa w29, 16
    .cfi_offset w30, -8
    .cfi_offset w29, -16
    sub     sp, sp, #0x100
    stp     x0, x1, [sp, #0x00]
    stp     x2, x3, [sp, #0x10]
    stp     x4, x5, [sp, #0x20]
    stp     x6, x7, [sp, #0x30]
    add     x9, x29, #16                /* sp beim Eintritt = erstes Stack-Argument */
    stp     x8, x9, [sp, #0x40]
    stp     q0, q1, [sp, #0x50]
    stp     q2, q3, [sp, #0x70]
    stp     q4, q5, [sp, #0x90]
    stp     q6, q7, [sp, #0xb0]
    mov     x0, sp
    add     x1, sp, #0xd0
    bl      ___CFArm64Forwarding
    cbnz    x0, 1f
    /* erledigt: Rückgabewert aus dem Rückgabe-Block laden */
    ldp     x0, x1, [sp, #0xd0]
    ldp     d0, d1, [sp, #0xe0]
    ldp     d2, d3, [sp, #0xf0]
    mov     sp, x29
    ldp     x29, x30, [sp], #16
    ret
1:  /* anderes Ziel (forwardingTargetForSelector:): Argumente zurück, neu senden */
    ldp     q0, q1, [sp, #0x50]
    ldp     q2, q3, [sp, #0x70]
    ldp     q4, q5, [sp, #0x90]
    ldp     q6, q7, [sp, #0xb0]
    ldr     x8, [sp, #0x40]
    ldp     x6, x7, [sp, #0x30]
    ldp     x4, x5, [sp, #0x20]
    ldp     x2, x3, [sp, #0x10]
    ldr     x1, [sp, #0x08]
    mov     sp, x29
    ldp     x29, x30, [sp], #16
    b       _objc_msgSend
    .cfi_endproc

    .globl  __CF_forwarding_prep_b
    .p2align 2
__CF_forwarding_prep_b:
    .cfi_startproc
    stp     x29, x30, [sp, #-16]!
    .cfi_def_cfa_offset 16
    mov     x29, sp
    .cfi_def_cfa w29, 16
    .cfi_offset w30, -8
    .cfi_offset w29, -16
    sub     sp, sp, #0x100
    stp     x0, x1, [sp, #0x00]
    stp     x2, x3, [sp, #0x10]
    stp     x4, x5, [sp, #0x20]
    stp     x6, x7, [sp, #0x30]
    add     x9, x29, #16
    stp     x8, x9, [sp, #0x40]
    stp     q0, q1, [sp, #0x50]
    stp     q2, q3, [sp, #0x70]
    stp     q4, q5, [sp, #0x90]
    stp     q6, q7, [sp, #0xb0]
    mov     x0, sp
    add     x1, sp, #0xd0
    bl      ___CFArm64BlockForwarding
    ldp     x0, x1, [sp, #0xd0]
    ldp     d0, d1, [sp, #0xe0]
    ldp     d2, d3, [sp, #0xf0]
    mov     sp, x29
    ldp     x29, x30, [sp], #16
    ret
    .cfi_endproc

/* --- Aufruf ---------------------------------------------------------------------------------
 * void __CFArm64Call(void *fn, const void *regs, const void *stack, size_t stacklen, void *ret)
 *   regs:  Rahmenkopf wie in NSInvocation (0x00 x0-x7, 0x40 x8, 0x50 d0-d7)
 *   stack: stacklen Byte (Vielfaches von 8), werden ab sp an den Aufruf gelegt */
    .private_extern ___CFArm64Call
    .globl  ___CFArm64Call
    .p2align 2
___CFArm64Call:
    .cfi_startproc
    stp     x20, x19, [sp, #-32]!
    stp     x29, x30, [sp, #16]
    add     x29, sp, #16
    .cfi_def_cfa w29, 16
    .cfi_offset w30, -8
    .cfi_offset w29, -16
    .cfi_offset w19, -24
    .cfi_offset w20, -32
    mov     x19, x4                     /* Rückgabe-Block */
    mov     x16, x0                     /* Ziel */
    mov     x17, x1                     /* Register */
    add     x9, x3, #15
    and     x9, x9, #~15
    sub     sp, sp, x9
    mov     x10, sp
    lsr     x3, x3, #3
    cbz     x3, 2f
1:  ldr     x11, [x2], #8
    str     x11, [x10], #8
    subs    x3, x3, #1
    b.ne    1b
2:  ldp     d0, d1, [x17, #0x50]
    ldp     d2, d3, [x17, #0x60]
    ldp     d4, d5, [x17, #0x70]
    ldp     d6, d7, [x17, #0x80]
    ldr     x8, [x17, #0x40]
    ldp     x6, x7, [x17, #0x30]
    ldp     x4, x5, [x17, #0x20]
    ldp     x2, x3, [x17, #0x10]
    ldp     x0, x1, [x17, #0x00]
    blr     x16
    stp     x0, x1, [x19, #0x00]
    stp     d0, d1, [x19, #0x10]
    stp     d2, d3, [x19, #0x20]
    sub     sp, x29, #16
    ldp     x29, x30, [sp, #16]
    ldp     x20, x19, [sp], #32
    ret
    .cfi_endproc

#endif
