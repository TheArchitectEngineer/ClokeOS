// statisches x86_64-Mach-O: schreibt dreimal "hallo von x86_64" über eine Unterfunktion, Exit-Code 7
.globl _start
.text
_start:
	movq $3, %r12
1:	call say
	subq $1, %r12
	jnz 1b
	movq $7, %rdi
	movq $0x2000001, %rax
	syscall
say:
	pushq %rbx
	movq $0x2000004, %rax
	movq $1, %rdi
	leaq msg(%rip), %rsi
	movq $len, %rdx
	syscall
	popq %rbx
	ret
.section __TEXT,__cstring
msg: .ascii "hallo von x86_64 auf arm64\n"
.set len, . - msg
