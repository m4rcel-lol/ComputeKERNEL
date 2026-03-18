.section .text
.global _start
.extern kmain

_start:
    cli
    lea boot_stack_top(%rip), %rsp
    xor %rbp, %rbp
    /* xor %rdi,%rdi zeroes %rdi, so boot_info argument is explicitly NULL for now. */
    xor %rdi, %rdi
    call kmain

halt_loop:
    hlt
    jmp halt_loop

.section .bss
.align 16
boot_stack:
    .skip 16384
boot_stack_top:
