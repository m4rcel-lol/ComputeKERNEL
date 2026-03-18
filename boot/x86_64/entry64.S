.section .text
.global _start
.extern kmain

_start:
    cli
    lea boot_stack_top(%rip), %rsp
    xor %rbp, %rbp
    call kmain

.halt:
    hlt
    jmp .halt

.section .bss
.align 16
boot_stack:
    .skip 16384
boot_stack_top:

