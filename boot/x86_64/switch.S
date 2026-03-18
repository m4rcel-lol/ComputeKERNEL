/*
 * boot/x86_64/switch.S
 *
 * Low-level context switch for the ComputeKERNEL round-robin scheduler.
 *
 * void switch_to(struct task *prev, struct task *next);
 *   rdi = prev task ptr
 *   rsi = next task ptr
 *
 * struct task layout (first two fields are rsp + stack_top):
 *   offset 0:  u64 rsp
 *   offset 8:  u64 stack_top
 *   ...
 *
 * We save callee-saved registers on prev's stack, store rsp, then
 * load next's rsp and restore its callee-saved registers.
 *
 * task_trampoline():
 *   Called the very first time a new task is scheduled.
 *   r12 = task function pointer, r13 = argument.
 */

.code64
.section .text
.global switch_to
switch_to:
    /* Save callee-saved registers on the current stack */
    pushq %rbp
    pushq %rbx
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    /* prev->rsp = %rsp  (offset 0 in struct task) */
    movq  %rsp, (%rdi)

    /* %rsp = next->rsp  (offset 0 in struct task) */
    movq  (%rsi), %rsp

    /* Restore callee-saved registers of 'next' task */
    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %rbx
    popq %rbp

    ret

/*
 * task_trampoline – entered the first time a task runs.
 * r12 holds the task function, r13 holds the argument.
 * We pass arg (r13) in rdi per the SysV calling convention, then call.
 */
.global task_trampoline
task_trampoline:
    movq  %r13, %rdi        /* arg → rdi (first parameter) */
    callq *%r12             /* call task_func(arg) */
    /* If fn returns, fall into task_exit() */
    callq task_exit

/* Suppress executable-stack warning */
.section .note.GNU-stack, "", @progbits

