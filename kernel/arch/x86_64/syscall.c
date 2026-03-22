/*
 * kernel/arch/x86_64/syscall.c
 *
 * SYSCALL/SYSRET fast-path setup and C-level syscall dispatcher.
 *
 * The SYSCALL instruction transfers control to the address in IA32_LSTAR.
 * It saves RIP in RCX and RFLAGS in R11, then clears RFLAGS bits specified
 * in IA32_FMASK.  SYSRET restores RIP from RCX and RFLAGS from R11.
 *
 * MSRs used:
 *   IA32_STAR   (0xC0000081) – segment selectors for SYSCALL/SYSRET
 *   IA32_LSTAR  (0xC0000082) – 64-bit SYSCALL entry RIP
 *   IA32_FMASK  (0xC0000084) – RFLAGS mask (bits to clear on SYSCALL)
 *   IA32_EFER   (0xC0000080) – must have SCE (bit 0) set
 */
#include <ck/syscall.h>
#include <ck/kernel.h>
#include <ck/vfs.h>
#include <ck/sched.h>
#include <ck/mm.h>
#include <ck/io.h>
#include <ck/types.h>
#include <ck/string.h>
#include <ck/arch/gdt.h>

/* MSR addresses */
#define MSR_EFER   0xC0000080
#define MSR_STAR   0xC0000081
#define MSR_LSTAR  0xC0000082
#define MSR_FMASK  0xC0000084

#define EFER_SCE   (1ULL << 0)   /* System Call Extensions enable */

/* Declared in boot/x86_64/syscall_entry.S */
extern void _syscall_entry(void);

/* ── Syscall handlers ───────────────────────────────────────────────── */

static s64 sys_write_impl(u64 fd, u64 buf_addr, u64 len)
{
    /* Only fd=1 (stdout) and fd=2 (stderr) supported for now */
    if (fd != 1 && fd != 2)
        return SYSCALL_ERR(EBADF);
    if (len == 0)
        return 0;
    if (len > 65536)
        len = 65536;

    /* buf_addr is a user pointer – in our current identity-mapped kernel
     * we can access it directly.  A real kernel would use copy_from_user. */
    const char *buf = (const char *)(uintptr_t)buf_addr;
    for (u64 i = 0; i < len; i++)
        ck_putchar(buf[i]);
    return (s64)len;
}

static s64 sys_read_impl(u64 fd, u64 buf_addr, u64 len)
{
    /* Only fd=0 (stdin) supported – reads from keyboard */
    if (fd != 0)
        return SYSCALL_ERR(EBADF);
    if (len == 0)
        return 0;

    char *buf = (char *)(uintptr_t)buf_addr;
    /* Non-blocking: return 0 if no key available */
    int c = 0;
    /* keyboard_getchar is declared in keyboard.h but we avoid the include
     * cycle by using an extern declaration here */
    extern int keyboard_getchar(void);
    c = keyboard_getchar();
    if (c == 0)
        return 0;
    buf[0] = (char)c;
    return 1;
}

static s64 sys_open_impl(u64 path_addr, u64 flags, u64 mode)
{
    (void)mode;
    const char *path = (const char *)(uintptr_t)path_addr;
    if (!path)
        return SYSCALL_ERR(EFAULT);
    int fd = vfs_open(path, (int)flags);
    return (fd < 0) ? SYSCALL_ERR(ENOENT) : (s64)fd;
}

static s64 sys_close_impl(u64 fd)
{
    int r = vfs_close((int)fd);
    return (r < 0) ? SYSCALL_ERR(EBADF) : 0;
}

static s64 sys_getpid_impl(void)
{
    struct task *t = sched_current();
    return t ? (s64)t->tid : 0;
}

static s64 sys_exit_impl(u64 code)
{
    (void)code;
    /* Mark current task dead */
    extern void task_exit(void);
    task_exit();
    __builtin_unreachable();
}

static s64 sys_yield_impl(void)
{
    sched_yield();
    return 0;
}

static s64 sys_mkdir_impl(u64 path_addr, u64 mode)
{
    (void)mode;
    const char *path = (const char *)(uintptr_t)path_addr;
    if (!path)
        return SYSCALL_ERR(EFAULT);
    int r = vfs_mkdir(path);
    return (r < 0) ? SYSCALL_ERR(EEXIST) : 0;
}

static s64 sys_unlink_impl(u64 path_addr)
{
    const char *path = (const char *)(uintptr_t)path_addr;
    if (!path)
        return SYSCALL_ERR(EFAULT);
    int r = vfs_unlink(path);
    return (r < 0) ? SYSCALL_ERR(ENOENT) : 0;
}

static s64 sys_rename_impl(u64 src_addr, u64 dst_addr)
{
    const char *src = (const char *)(uintptr_t)src_addr;
    const char *dst = (const char *)(uintptr_t)dst_addr;
    if (!src || !dst)
        return SYSCALL_ERR(EFAULT);
    int r = vfs_rename(src, dst);
    return (r < 0) ? SYSCALL_ERR(ENOENT) : 0;
}

static s64 sys_uptime_impl(void)
{
    extern u64 pit_get_ticks(void);
    return (s64)pit_get_ticks();
}

/* ── Dispatcher ─────────────────────────────────────────────────────── */

s64 syscall_dispatch(struct syscall_frame *frame)
{
    u64 nr  = frame->rax;
    u64 a1  = frame->rdi;
    u64 a2  = frame->rsi;
    u64 a3  = frame->rdx;

    switch (nr) {
    case SYS_READ:    return sys_read_impl(a1, a2, a3);
    case SYS_WRITE:   return sys_write_impl(a1, a2, a3);
    case SYS_OPEN:    return sys_open_impl(a1, a2, frame->rdx);
    case SYS_CLOSE:   return sys_close_impl(a1);
    case SYS_GETPID:  return sys_getpid_impl();
    case SYS_EXIT:    return sys_exit_impl(a1);
    case SYS_YIELD:   return sys_yield_impl();
    case SYS_MKDIR:   return sys_mkdir_impl(a1, a2);
    case SYS_UNLINK:  return sys_unlink_impl(a1);
    case SYS_RENAME:  return sys_rename_impl(a1, a2);
    case SYS_UPTIME:  return sys_uptime_impl();
    default:
        return SYSCALL_ERR(ENOSYS);
    }
}

/* ── MSR setup ──────────────────────────────────────────────────────── */

void syscall_init(void)
{
    /* Enable SCE in EFER */
    u64 efer = rdmsr(MSR_EFER);
    wrmsr(MSR_EFER, efer | EFER_SCE);

    /*
     * STAR layout (bits 63:48 = SYSRET CS/SS base, bits 47:32 = SYSCALL CS/SS):
     *   SYSCALL: CS = STAR[47:32],     SS = STAR[47:32] + 8
     *   SYSRET:  CS = STAR[63:48] + 16, SS = STAR[63:48] + 8
     *
     * We set SYSCALL CS = GDT_KCODE (0x08), SYSRET CS base = GDT_UCODE32 (0x18).
     * This gives: SYSRET 64-bit CS = 0x18+16 = 0x28 (GDT_UCODE64), SS = 0x18+8 = 0x20.
     */
    u64 star = ((u64)GDT_UCODE32 << 48) | ((u64)GDT_KCODE << 32);
    wrmsr(MSR_STAR, star);

    /* LSTAR: 64-bit SYSCALL entry point */
    wrmsr(MSR_LSTAR, (u64)(uintptr_t)_syscall_entry);

    /* FMASK: clear IF (bit 9) and DF (bit 10) on SYSCALL entry */
    wrmsr(MSR_FMASK, (1ULL << 9) | (1ULL << 10));

    ck_puts("[arch] SYSCALL/SYSRET fast path enabled\n");
}
