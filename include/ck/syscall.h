/*
 * include/ck/syscall.h
 *
 * Syscall numbers and kernel-side dispatcher interface.
 *
 * The SYSCALL/SYSRET fast path is set up in kernel/arch/x86_64/syscall.c.
 * User-space invokes syscalls via the SYSCALL instruction:
 *   rax = syscall number
 *   rdi, rsi, rdx, r10, r8, r9 = arguments (Linux ABI compatible)
 *   return value in rax (negative errno on error)
 */
#ifndef CK_SYSCALL_H
#define CK_SYSCALL_H

#include <ck/types.h>

/* ── Syscall numbers ────────────────────────────────────────────────── */
#define SYS_READ        0
#define SYS_WRITE       1
#define SYS_OPEN        2
#define SYS_CLOSE       3
#define SYS_STAT        4
#define SYS_FSTAT       5
#define SYS_LSEEK       8
#define SYS_MMAP        9
#define SYS_MUNMAP      11
#define SYS_BRK         12
#define SYS_GETPID      39
#define SYS_FORK        57
#define SYS_EXECVE      59
#define SYS_EXIT        60
#define SYS_WAIT4       61
#define SYS_UNAME       63
#define SYS_GETDENTS    78
#define SYS_GETCWD      79
#define SYS_CHDIR       80
#define SYS_MKDIR       83
#define SYS_RMDIR       84
#define SYS_UNLINK      87
#define SYS_RENAME      82
#define SYS_YIELD       158   /* sched_yield */
#define SYS_UPTIME      200   /* ComputeKERNEL extension */
#define SYS_REBOOT      169

/* ── Error codes ────────────────────────────────────────────────────── */
#define EPERM    1
#define ENOENT   2
#define ESRCH    3
#define EINTR    4
#define EIO      5
#define ENXIO    6
#define EBADF    9
#define ENOMEM   12
#define EACCES   13
#define EFAULT   14
#define EBUSY    16
#define EEXIST   17
#define ENODEV   19
#define ENOTDIR  20
#define EISDIR   21
#define EINVAL   22
#define ENFILE   23
#define EMFILE   24
#define ENOSPC   28
#define ERANGE   34
#define ENOSYS   38
#define ENOTEMPTY 39

/* Encode a negative errno return value */
#define SYSCALL_ERR(e)  (-(s64)(e))

/* ── Kernel-side interface ──────────────────────────────────────────── */

/* Saved register state on syscall entry */
struct syscall_frame {
    u64 rax;   /* syscall number / return value */
    u64 rdi;   /* arg1 */
    u64 rsi;   /* arg2 */
    u64 rdx;   /* arg3 */
    u64 r10;   /* arg4 (replaces rcx which SYSCALL clobbers) */
    u64 r8;    /* arg5 */
    u64 r9;    /* arg6 */
    u64 rcx;   /* saved RIP (by SYSCALL instruction) */
    u64 r11;   /* saved RFLAGS (by SYSCALL instruction) */
    u64 rsp;   /* user stack pointer */
};

/* Initialise SYSCALL/SYSRET MSRs */
void syscall_init(void);

/* C-level dispatcher – called from the assembly entry stub */
s64 syscall_dispatch(struct syscall_frame *frame);

#endif /* CK_SYSCALL_H */
