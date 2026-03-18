/*
 * boot/x86_64/isr_stubs.S
 *
 * Generates one tiny stub per interrupt vector (0-255).
 * Each stub normalises the stack so the C dispatcher always sees:
 *
 *   [rsp+ 0]  r15
 *   [rsp+ 8]  r14
 *   [rsp+16]  r13
 *   [rsp+24]  r12
 *   [rsp+32]  r11
 *   [rsp+40]  r10
 *   [rsp+48]  r9
 *   [rsp+56]  r8
 *   [rsp+64]  rbp
 *   [rsp+72]  rdi
 *   [rsp+80]  rsi
 *   [rsp+88]  rdx
 *   [rsp+96]  rcx
 *   [rsp+104] rbx
 *   [rsp+112] rax
 *   [rsp+120] vector
 *   [rsp+128] error_code   (real or dummy 0)
 *   [rsp+136] rip   \
 *   [rsp+144] cs     |  pushed by CPU
 *   [rsp+152] rflags |
 *   [rsp+160] rsp    |  (only on CPL change)
 *   [rsp+168] ss    /
 *
 * Exceptions that push a real error code: 8,10,11,12,13,14,17,21,29,30
 * All others get a dummy 0 pushed first.
 */

/* Macro: exception WITHOUT an error code */
.macro isr_noerr vec
.global _isr\vec
_isr\vec:
    pushq $0          /* dummy error code */
    pushq $\vec
    jmp   _isr_common
.endm

/* Macro: exception WITH an error code (CPU already pushed it) */
.macro isr_err vec
.global _isr\vec
_isr\vec:
    pushq $\vec
    jmp   _isr_common
.endm

/* CPU exceptions ─────────────────────────────────────────────────── */
isr_noerr  0   /* #DE Divide Error */
isr_noerr  1   /* #DB Debug */
isr_noerr  2   /* NMI */
isr_noerr  3   /* #BP Breakpoint */
isr_noerr  4   /* #OF Overflow */
isr_noerr  5   /* #BR Bound Range */
isr_noerr  6   /* #UD Invalid Opcode */
isr_noerr  7   /* #NM Device Not Available */
isr_err    8   /* #DF Double Fault (error code = 0) */
isr_noerr  9   /* Coprocessor Segment Overrun (legacy) */
isr_err   10   /* #TS Invalid TSS */
isr_err   11   /* #NP Segment Not Present */
isr_err   12   /* #SS Stack-Segment Fault */
isr_err   13   /* #GP General Protection */
isr_err   14   /* #PF Page Fault */
isr_noerr 15   /* Reserved */
isr_noerr 16   /* #MF x87 FPU */
isr_err   17   /* #AC Alignment Check */
isr_noerr 18   /* #MC Machine Check */
isr_noerr 19   /* #XM SIMD FP */
isr_noerr 20   /* #VE Virtualization */
isr_err   21   /* #CP Control Protection */
isr_noerr 22
isr_noerr 23
isr_noerr 24
isr_noerr 25
isr_noerr 26
isr_noerr 27
isr_noerr 28
isr_err   29   /* #VC VMM Communication */
isr_err   30   /* #SX Security Exception */
isr_noerr 31

/* IRQ stubs (vectors 32-47 after PIC remapping) */
isr_noerr 32   /* IRQ0  - PIT timer */
isr_noerr 33   /* IRQ1  - PS/2 keyboard */
isr_noerr 34   /* IRQ2  - cascade */
isr_noerr 35   /* IRQ3  - COM2 */
isr_noerr 36   /* IRQ4  - COM1 */
isr_noerr 37   /* IRQ5  - LPT2 */
isr_noerr 38   /* IRQ6  - floppy */
isr_noerr 39   /* IRQ7  - LPT1 */
isr_noerr 40   /* IRQ8  - RTC */
isr_noerr 41   /* IRQ9  - free */
isr_noerr 42   /* IRQ10 - free */
isr_noerr 43   /* IRQ11 - free */
isr_noerr 44   /* IRQ12 - PS/2 mouse */
isr_noerr 45   /* IRQ13 - FPU */
isr_noerr 46   /* IRQ14 - ATA primary */
isr_noerr 47   /* IRQ15 - ATA secondary */

/* Spurious / software vectors */
isr_noerr 48
isr_noerr 49
isr_noerr 50
isr_noerr 51
isr_noerr 52
isr_noerr 53
isr_noerr 54
isr_noerr 55
isr_noerr 56
isr_noerr 57
isr_noerr 58
isr_noerr 59
isr_noerr 60
isr_noerr 61
isr_noerr 62
isr_noerr 63
isr_noerr 64
isr_noerr 255  /* APIC spurious */

/* ── Common stub ─────────────────────────────────────────────────── */
/*
 * At entry: stack has [error_code, vector, rip, cs, rflags, (rsp, ss)]
 * We save all GPRs, pass frame pointer to interrupt_dispatch(), restore.
 *
 * rbx is callee-saved per SysV ABI, so we stash the pre-alignment rsp
 * there (we've already saved rbx's original value on the stack).
 */
.code64
.section .text
.global _isr_common
_isr_common:
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq  %rsp, %rdi     /* arg1: pointer to interrupt_frame */
    movq  %rsp, %rbx     /* stash frame ptr in callee-saved rbx */
    andq  $~15, %rsp     /* 16-byte-align stack for the C call */
    call  interrupt_dispatch
    movq  %rbx, %rsp     /* restore frame pointer */

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp       /* discard vector + error_code */
    iretq

/* Suppress the executable-stack linker warning */
.section .note.GNU-stack, "", @progbits

