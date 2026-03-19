#ifndef CK_KERNEL_H
#define CK_KERNEL_H

#include <ck/types.h>
#include <ck/string.h>

/* ── Early console (VGA text + serial) ─────────────────────────────── */
void ck_early_console_init(void);
void ck_console_clear(void);
void ck_putchar(char c);
void ck_puts(const char *s);
void ck_printk(const char *fmt, ...) __attribute__((format(printf, 1, 2)));

/* VGA foreground color constants (background is always black) */
#define CK_COLOR_BLACK        0
#define CK_COLOR_BLUE         1
#define CK_COLOR_GREEN        2
#define CK_COLOR_CYAN         3
#define CK_COLOR_RED          4
#define CK_COLOR_MAGENTA      5
#define CK_COLOR_BROWN        6
#define CK_COLOR_LIGHT_GRAY   7
#define CK_COLOR_DARK_GRAY    8
#define CK_COLOR_LIGHT_BLUE   9
#define CK_COLOR_LIGHT_GREEN  10
#define CK_COLOR_LIGHT_CYAN   11
#define CK_COLOR_LIGHT_RED    12
#define CK_COLOR_PINK         13
#define CK_COLOR_YELLOW       14
#define CK_COLOR_WHITE        15

void ck_set_color(u8 fg_color);
void ck_reset_color(void);

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

/* ── Keyboard layout API ─────────────────────────────────────────────── */
int         keyboard_get_layout_count(void);
const char *keyboard_get_layout_name(void);
const char *keyboard_get_layout_name_at(int i);
const char *keyboard_get_layout_desc_at(int i);
int         keyboard_get_sublayout_count_at(int layout_i);
const char *keyboard_get_sublayout_name_at(int layout_i, int sub_i);
const char *keyboard_get_sublayout_desc_at(int layout_i, int sub_i);
int         keyboard_get_sublayout_idx(void);
int         keyboard_set_layout(const char *name);
void        keyboard_set_sublayout(int sub_i);

/* ── Kernel main entry (multiboot2) ─────────────────────────────────── */
void kmain(unsigned int mb2_magic, unsigned int mb2_info_phys);

/* ── Panic ──────────────────────────────────────────────────────────── */
void ck_panic(const char *msg) __attribute__((noreturn));

#endif /* CK_KERNEL_H */

