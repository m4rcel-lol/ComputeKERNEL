#ifndef CK_KERNEL_H
#define CK_KERNEL_H

/* Kernel main entry point — receives multiboot2 magic and info pointer */
void kmain(unsigned int mb2_magic, unsigned int mb2_info);

/* Early VGA text console */
void ck_early_console_init(void);
void ck_putchar(char c);
void ck_puts(const char *s);
void ck_printk(const char *fmt, ...);

/* Subsystem stubs (implemented in kernel/stubs.c) */
void mm_early_init(void *boot_info);
void arch_init(void);
void sched_init(void);
void vfs_init(void);
void init_spawn_first_user(const char *path);
void arch_halt(void);

#endif /* CK_KERNEL_H */

