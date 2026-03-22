/*
 * boot/x86_64/syscall_entry.S
 *
 * SYSCALL fast-path entry stub for ComputeKERNEL.
 *
 * On SYSCALL entry (hardware state):
 *   RCX    = saved user RIP  (SYSCALL saves it here)
 *   R11    = saved user RFLAGS
 *   RSP    = user stack (unchanged)
 *   CS/SS  = kernel selectors (from IA32_STAR)
 *   IF     = 0 (cleared by IA32_FMASK)
 *
 * We switch to a static kernel stack, build a syscall_frame, call the
 * C dispatcher, then SYSRET back.
 *
 * struct syscall_frame (include/ck/syscall.h) – pushed low→high:
 *   [rsp+ 0] rax   (syscall number / return value)
 *   [rsp+ 8] rdi   (arg1)
 *   [rsp+16] rsi   (arg2)
 *   [rsp+24] rdx   (arg3)
 *   [rsp+32] r10   (arg4)
 *   [rsp+40] r8    (arg5)
 *   [rsp+48] r9    (arg6)
 *   [rsp+56] rcx   (saved user RIP)
 *   [rsp+64] r11   (saved user RFLAGS)
 *   [rsp+72] rsp   (saved user RSP)
 */

.code64
.section .text
.global _syscall_entry
_syscall_entry:
    /* 1. Save user RSP and switch to kernel syscall stack */
    movq  %rsp, (syscall_user_rsp)
    leaq  syscall_kstack_top(%rip), %rsp

    /* 2. Build syscall_frame (push in reverse field order) */
    pushq (syscall_user_rsp)   /* rsp  – user stack pointer */
    pushq %r11                 /* r11  – saved RFLAGS */
    pushq %rcx                 /* rcx  – saved RIP */
    pushq %r9                  /* r9   – arg6 */
    pushq %r8                  /* r8   – arg5 */
    pushq %r10                 /* r10  – arg4 */
    pushq %rdx                 /* rdx  – arg3 */
    pushq %rsi                 /* rsi  – arg2 */
    pushq %rdi                 /* rdi  – arg1 */
    pushq %rax                 /* rax  – syscall number */

    /* 3. Call C dispatcher with frame pointer as first argument */
    movq  %rsp, %rdi           /* arg1 = &frame */
    movq  %rsp, %rbx           /* stash frame ptr in callee-saved rbx */
    andq  $~15, %rsp           /* 16-byte align */
    call  syscall_dispatch     /* returns s64 retval in rax */
    movq  %rbx, %rsp           /* restore frame pointer */

    /* 4. Store return value into frame->rax */
    movq  %rax, (%rsp)         /* frame->rax = retval */

    /* 5. Restore registers from frame */
    popq  %rax                 /* syscall return value */
    popq  %rdi
    popq  %rsi
    popq  %rdx
    popq  %r10
    popq  %r8
    popq  %r9
    popq  %rcx                 /* saved RIP → rcx for SYSRET */
    popq  %r11                 /* saved RFLAGS → r11 for SYSRET */
    popq  %rsp                 /* restore user RSP */

    /* 6. Return to user space */
    sysretq

/* ── Static kernel syscall stack (8 KiB) ───────────────────────────── */
.section .bss
.align 16
syscall_kstack:
    .space 8192
syscall_kstack_top:

syscall_user_rsp:
    .quad 0

/* Suppress executable-stack warning */
.section .note.GNU-stack, "", @progbits
