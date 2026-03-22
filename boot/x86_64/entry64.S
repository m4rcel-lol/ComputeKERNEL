/*
 * boot/x86_64/entry64.S
 *
 * Limine boot protocol entry for ComputeKERNEL.
 * Limine transfers control here in 64-bit long mode.
 */

#include <limine.h>

/* ── Limine Requests ───────────────────────────────────────────────── */
.section .limine_requests, "a"
.align 8

.global limine_base_revision
limine_base_revision:
    .quad 0xf9562b2d5c95a6c8, 0x6a7b38494557e315 /* id */
    .quad 0                                      /* revision */
    .quad 0                                      /* response */

.global limine_framebuffer_request
limine_framebuffer_request:
    .quad 0x9d5827dcd881aa74, 0xce5e25243394b6d0 /* id */
    .quad 0                                      /* revision */
    .quad 0                                      /* response */

.global limine_memmap_request
limine_memmap_request:
    .quad 0x67cf3d9d378a8016, 0x9d5430d459fd7475 /* id */
    .quad 0                                      /* revision */
    .quad 0                                      /* response */

.global limine_hhdm_request
limine_hhdm_request:
    .quad 0xb029f4620d329188, 0xe175069b2b36e2d1 /* id */
    .quad 0                                      /* revision */
    .quad 0                                      /* response */

.global limine_kernel_address_request
limine_kernel_address_request:
    .quad 0x71ba76863bc300f0, 0xbc4a44065e3027fa /* id */
    .quad 0                                      /* revision */
    .quad 0                                      /* response */

/* Terminating tag */
.quad 0, 0

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
