#ifndef CK_KERNEL_H
#define CK_KERNEL_H

void ck_early_console_init(void);
void ck_printk(const char *fmt, ...);
void mm_early_init(void *boot_info);
void arch_init(void);
void sched_init(void);
void vfs_init(void);
void init_spawn_first_user(const char *path);
void arch_halt(void);

#endif /* CK_KERNEL_H */

