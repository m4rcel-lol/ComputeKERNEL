/*
 * boot/x86_64/entry64.S
 *
 * Limine boot protocol entry for ComputeKERNEL.
 * Limine transfers control here in 64-bit long mode.
 */

/* ── 64-bit entry point ────────────────────────────────────────────── */
.section .text
.code64
.global _start
_start:
    cli

    /* Reload data/stack segment registers */
    /* Limine already sets up a basic GDT, but we'll load our own in arch_init */
    xor %ax, %ax
    mov %ax, %ds
    mov %ax, %es
    mov %ax, %ss
    mov %ax, %fs
    mov %ax, %gs

    /* Switch to the kernel stack */
    lea kernel_stack_top(%rip), %rsp

    /* 
     * Limine doesn't pass arguments in registers like Multiboot2.
     * We access request responses directly in C via the global symbols.
     */
    call kmain

    /* kmain must not return; halt if it does */
.global _halt
_halt:
    cli
1:  hlt
    jmp 1b

/* ── .bss: stack ───────────────────────────────────────────────────── */
.section .bss
.align 16
kernel_stack_bottom: .skip 65536          /* 64 KiB kernel stack */
kernel_stack_top:

/* Mark the stack as non-executable */
.section .note.GNU-stack, "", @progbits
