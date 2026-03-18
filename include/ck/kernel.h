#ifndef CK_KERNEL_H
#define CK_KERNEL_H

#include <ck/types.h>
#include <ck/string.h>

/* ── Early console (VGA text + serial) ─────────────────────────────── */
void ck_early_console_init(void);
void ck_putchar(char c);
void ck_puts(const char *s);
void ck_printk(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* Serial port (COM1) */
void serial_init(void);
void serial_putchar(char c);
void serial_puts(const char *s);

/* ── Architecture init ──────────────────────────────────────────────── */
void arch_init(void);                 /* GDT + IDT + PIC + PIT */
void arch_halt(void) __attribute__((noreturn));

/* ── Memory management ──────────────────────────────────────────────── */
void mm_early_init(void *mb2_info);   /* PMM init (parses multiboot2) */
void heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);
void  kfree(void *ptr);

/* ── Scheduler ──────────────────────────────────────────────────────── */
void sched_init(void);
void sched_yield(void);

/* ── Virtual filesystem ─────────────────────────────────────────────── */
void vfs_init(void);

/* ── Kernel main entry (multiboot2) ─────────────────────────────────── */
void kmain(unsigned int mb2_magic, unsigned int mb2_info_phys);

/* ── Panic ──────────────────────────────────────────────────────────── */
void ck_panic(const char *msg) __attribute__((noreturn));

#endif /* CK_KERNEL_H */

