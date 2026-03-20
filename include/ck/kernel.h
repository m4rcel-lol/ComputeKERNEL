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
void ck_console_scroll_up(u32 lines);
void ck_console_scroll_down(u32 lines);
void ck_console_scroll_reset(void);
int  ck_display_set_resolution(u32 width, u32 height);
void ck_display_get_resolution(u32 *width, u32 *height);

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
u32 ck_boot_network_packet_size(void);
const u8 *ck_boot_network_packet_data(void);
int ck_network_available(void);

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
