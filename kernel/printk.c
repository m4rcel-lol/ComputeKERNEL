#include <ck/kernel.h>

#define VGA_BASE  ((volatile unsigned short *)0xb8000)
#define VGA_COLS  80
#define VGA_ROWS  25
#define VGA_ATTR  0x0f00   /* white on black */

static int col = 0, row = 0;

static void vga_scroll(void)
{
    volatile unsigned short *vga = VGA_BASE;
    int r, c;

    for (r = 0; r < VGA_ROWS - 1; r++)
        for (c = 0; c < VGA_COLS; c++)
            vga[r * VGA_COLS + c] = vga[(r + 1) * VGA_COLS + c];

    for (c = 0; c < VGA_COLS; c++)
        vga[(VGA_ROWS - 1) * VGA_COLS + c] = VGA_ATTR | ' ';

    row = VGA_ROWS - 1;
}

void ck_early_console_init(void)
{
    volatile unsigned short *vga = VGA_BASE;
    int i;

    for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
        vga[i] = VGA_ATTR | ' ';
    col = 0;
    row = 0;
}

void ck_putchar(char c)
{
    volatile unsigned short *vga = VGA_BASE;

    if (c == '\n') {
        col = 0;
        row++;
    } else {
        vga[row * VGA_COLS + col] = VGA_ATTR | (unsigned char)c;
        if (++col >= VGA_COLS) {
            col = 0;
            row++;
        }
    }
    if (row >= VGA_ROWS)
        vga_scroll();
}

void ck_puts(const char *s)
{
    while (*s)
        ck_putchar(*s++);
}

void ck_printk(const char *fmt, ...)
{
    /* Basic passthrough for string literals (no format expansion yet) */
    ck_puts(fmt);
}

