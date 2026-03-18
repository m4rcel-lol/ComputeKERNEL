/*
 * boot/x86_64/entry64.S
 *
 * Multiboot2 header + 32-bit → 64-bit long-mode entry for ComputeKERNEL.
 * GRUB (BIOS boot) transfers control here in 32-bit protected mode.
 * We set up page tables, enable long mode, then jump to 64-bit kernel code.
 */

/* ── Multiboot2 header ───────────────────────────────────────────────── */
.section .multiboot2, "a"
.align 8
mb2_hdr_start:
    .long  0xe85250d6                                          /* magic   */
    .long  0                                                   /* arch: i386 protected mode */
    .long  (mb2_hdr_end - mb2_hdr_start)                      /* header length */
    .long  -(0xe85250d6 + 0 + (mb2_hdr_end - mb2_hdr_start))  /* checksum */
    /* End tag (type=0, flags=0, size=8) */
    .word  0
    .word  0
    .long  8
mb2_hdr_end:

/* ── 64-bit GDT used after the long-mode switch ──────────────────────── */
/*
 * Each 64-bit GDT entry is encoded as: [Limit 15:0][Base 23:0][Type][S P DPL][Limit 19:16 AVL L D/B G][Base 31:24][Base 63:32]
 * In 64-bit mode, base and limit are ignored for code/data; only the
 * L (long-mode), P (present), S (descriptor type), and Type fields matter.
 *   0x00af9a000000ffff  → P=1, DPL=0, S=1, Type=0xA (exec/read), L=1, D/B=0
 *   0x00af92000000ffff  → P=1, DPL=0, S=1, Type=0x2 (read/write), L=1, D/B=0
 */
.section .rodata
.align 16
gdt64:
    .8byte 0x0000000000000000   /* 0x00: null descriptor */
    .8byte 0x00af9a000000ffff   /* 0x08: 64-bit code, DPL=0 (L=1,P=1,S=1,Type=0xA) */
    .8byte 0x00af92000000ffff   /* 0x10: 64-bit data, DPL=0 (P=1,S=1,Type=0x2) */
gdt64_end:

.align 4
gdt64_ptr:
    .word  gdt64_end - gdt64 - 1   /* limit */
    .long  gdt64                    /* 32-bit base (kernel is below 4 GiB) */

/* ── 32-bit protected-mode entry (GRUB drops us here) ────────────────── */
.section .text.entry32
.code32
.global _start
_start:
    cli

    /* Set up a small 32-bit stack for this transition phase */
    mov   $boot32_stack_top, %esp

    /* Save multiboot2 magic (eax) and info-structure pointer (ebx) */
    mov   %eax, mb2_magic_saved
    mov   %ebx, mb2_info_saved

    /* ── Zero PML4 + PDPT + PD (3 × 4 KiB) ── */
    cld
    mov   $pml4,           %edi
    xor   %eax,            %eax
    mov   $(3 * 4096 / 4), %ecx
    rep   stosl

    /* ── PML4[0] → PDPT (present + writable) ── */
    mov   $pdpt, %eax
    or    $3,    %eax
    mov   %eax,  (pml4)

    /* ── PDPT[0] → PD (present + writable) ── */
    mov   $pd,  %eax
    or    $3,   %eax
    mov   %eax, (pdpt)

    /* ── PD: 512 × 2 MiB huge pages, identity-mapping first 1 GiB ── */
    mov   $pd,   %edi
    xor   %eax,  %eax
    or    $0x83, %eax          /* Present + Writable + Page Size (2 MiB) */
    mov   $512,  %ecx
1:  movl  %eax, (%edi)
    movl  $0,   4(%edi)
    add   $8,   %edi
    add   $0x200000, %eax
    loop  1b

    /* ── Load PML4 into CR3 ── */
    mov   $pml4, %eax
    mov   %eax,  %cr3

    /* ── Enable PAE (CR4.PAE) ── */
    mov   %cr4,  %eax
    or    $0x20, %eax
    mov   %eax,  %cr4

    /* ── Set EFER.LME (MSR 0xC0000080) ── */
    mov   $0xc0000080, %ecx
    rdmsr
    or    $0x100, %eax
    wrmsr

    /* ── Enable paging + protected mode (CR0.PG | CR0.PE) ── */
    mov   %cr0,        %eax
    or    $0x80000001, %eax
    mov   %eax,        %cr0

    /* ── Load our 64-bit GDT ── */
    lgdt  gdt64_ptr

    /* ── Far-jump to 64-bit code segment (selector 0x08) ── */
    ljmp  $0x08, $entry64

/* ── 64-bit long-mode entry ──────────────────────────────────────────── */
.code64
.section .text
.global entry64
entry64:
    /* Reload data/stack segment registers with our 64-bit data selector */
    mov   $0x10, %ax
    mov   %ax,   %ds
    mov   %ax,   %es
    mov   %ax,   %ss
    xor   %ax,   %ax
    mov   %ax,   %fs
    mov   %ax,   %gs

    /* Switch to the kernel stack */
    lea   kernel_stack_top(%rip), %rsp

    /* Pass saved multiboot2 values to kmain (SysV64 ABI: rdi, rsi) */
    movl  mb2_magic_saved(%rip), %edi
    movl  mb2_info_saved(%rip),  %esi

    call  kmain

    /* kmain must not return; halt if it does */
.global _halt
_halt:
    cli
    hlt
    jmp   _halt

/* ── .data: temporary multiboot2 storage ────────────────────────────── */
.section .data
.align 4
mb2_magic_saved: .long 0
mb2_info_saved:  .long 0

/* ── .bss: stacks first, then page tables ────────────────────────────
 *
 * IMPORTANT: stacks grow downward.  Placing them BEFORE the page tables
 * means any overflow extends away from pml4/pdpt/pd rather than into them.
 * With the original order (page tables first, stacks above) a stack that
 * grew past its reservation would silently corrupt the live page structure,
 * causing a page fault whose handler was also unreachable → triple fault.
 *
 * The kernel stack is 64 KiB (up from 16 KiB) to give deep call chains
 * enough headroom before the guard region.
 */
.section .bss
.align 16
boot32_stack_bottom: .skip 4096
boot32_stack_top:

.align 16
kernel_stack_bottom: .skip 65536          /* 64 KiB kernel stack */
kernel_stack_top:

/* Page tables sit at the top of BSS; downward stack growth cannot reach them */
.align 4096
pml4:  .skip 4096
pdpt:  .skip 4096
pd:    .skip 4096

/* Mark the stack as non-executable (suppresses linker warning) */
.section .note.GNU-stack, "", @progbits
