#include <ck/kernel.h>
#include <ck/io.h>
#include <ck/types.h>
#include <ck/string.h>
#include <stdarg.h>

/* ── VGA text console ───────────────────────────────────────────────── */
#define VGA_BASE        ((volatile unsigned short *)0xb8000)
#define VGA_COLS        80
#define VGA_ROWS        25
#define VGA_COLOR_WHITE 0x0f   /* white on black (default) */

static int col = 0, row = 0;
static u8  vga_color = VGA_COLOR_WHITE;

static void vga_update_cursor(void)
{
    u16 pos = (u16)(row * VGA_COLS + col);
    outb(0x3d4, 0x0e);
    outb(0x3d5, (u8)(pos >> 8));
    outb(0x3d4, 0x0f);
    outb(0x3d5, (u8)pos);
}

static void vga_scroll(void)
{
    volatile unsigned short *vga = VGA_BASE;
    int r, c;

    for (r = 0; r < VGA_ROWS - 1; r++)
        for (c = 0; c < VGA_COLS; c++)
            vga[r * VGA_COLS + c] = vga[(r + 1) * VGA_COLS + c];

    for (c = 0; c < VGA_COLS; c++)
        vga[(VGA_ROWS - 1) * VGA_COLS + c] = ((unsigned short)VGA_COLOR_WHITE << 8) | ' ';

    row = VGA_ROWS - 1;
    vga_update_cursor();
}

void ck_early_console_init(void)
{
    volatile unsigned short *vga = VGA_BASE;
    int i;

    for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
        vga[i] = ((unsigned short)VGA_COLOR_WHITE << 8) | ' ';
    col = 0;
    row = 0;
    vga_color = VGA_COLOR_WHITE;
    vga_update_cursor();
}

void ck_console_clear(void)
{
    ck_early_console_init();
}

void ck_set_color(u8 fg_color)
{
    vga_color = fg_color;
}

void ck_reset_color(void)
{
    vga_color = VGA_COLOR_WHITE;
}

void ck_putchar(char c)
{
    volatile unsigned short *vga = VGA_BASE;
    unsigned short attr = (unsigned short)vga_color << 8;

    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\r') {
        col = 0;
    } else if (c == '\b') {
        if (col > 0) {
            col--;
        } else if (row > 0) {
            row--;
            col = VGA_COLS - 1;
        }
        vga[row * VGA_COLS + col] = attr | ' ';
        vga_update_cursor();
        return;
    } else if (c == '\t') {
        col = (col + 8) & ~7;
        if (col >= VGA_COLS) {
            col = 0;
            row++;
        }
    } else {
        vga[row * VGA_COLS + col] = attr | (unsigned char)c;
        if (++col >= VGA_COLS) {
            col = 0;
            row++;
        }
    }
    if (row >= VGA_ROWS)
        vga_scroll();
    else
        vga_update_cursor();
}

void ck_puts(const char *s)
{
    while (*s)
        ck_putchar(*s++);
}

/* ── Full printf-style formatter ────────────────────────────────────── */

/*
 * Supported specifiers: %c %s %d %i %u %x %X %p %% %z (size_t)
 * Width and zero-padding: %08x, %-10s, etc.
 */
void ck_printk(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);

    while (*fmt) {
        if (*fmt != '%') {
            ck_putchar(*fmt++);
            continue;
        }
        fmt++;  /* skip '%' */

        /* Flags */
        int zero_pad = 0, left_align = 0;
        while (*fmt == '0' || *fmt == '-') {
            if (*fmt == '0') zero_pad  = 1;
            if (*fmt == '-') left_align = 1;
            fmt++;
        }

        /* Width */
        int width = 0;
        while (*fmt >= '0' && *fmt <= '9')
            width = width * 10 + (*fmt++ - '0');

        /* Precision (ignored, consumed) */
        if (*fmt == '.') {
            fmt++;
            while (*fmt >= '0' && *fmt <= '9') fmt++;
        }

        /* Length modifier */
        int is_long = 0, is_size = 0;
        if (*fmt == 'l') { is_long = 1; fmt++; if (*fmt == 'l') fmt++; }
        else if (*fmt == 'z') { is_size = 1; fmt++; }
        (void)is_size; /* used implicitly via u64 path */

        char   buf[32];
        int    blen;
        char   pad_ch = (zero_pad && !left_align) ? '0' : ' ';

        switch (*fmt) {
        case 'c': {
            char ch = (char)va_arg(ap, int);
            if (!left_align)
                for (int i = 1; i < width; i++) ck_putchar(pad_ch);
            ck_putchar(ch);
            if (left_align)
                for (int i = 1; i < width; i++) ck_putchar(' ');
            break;
        }
        case 's': {
            const char *s = va_arg(ap, const char *);
            if (!s) s = "(null)";
            int slen = (int)strlen(s);
            if (!left_align)
                for (int i = slen; i < width; i++) ck_putchar(' ');
            ck_puts(s);
            if (left_align)
                for (int i = slen; i < width; i++) ck_putchar(' ');
            break;
        }
        case 'd':
        case 'i': {
            s64 val = is_long ? (s64)va_arg(ap, long long) : (s64)va_arg(ap, int);
            blen = itoa_dec(val, buf);
            if (!left_align)
                for (int i = blen; i < width; i++) ck_putchar(pad_ch);
            ck_puts(buf);
            if (left_align)
                for (int i = blen; i < width; i++) ck_putchar(' ');
            break;
        }
        case 'u': {
            u64 val = is_long ? va_arg(ap, unsigned long long) : (u64)va_arg(ap, unsigned int);
            blen = utoa_dec(val, buf);
            if (!left_align)
                for (int i = blen; i < width; i++) ck_putchar(pad_ch);
            ck_puts(buf);
            if (left_align)
                for (int i = blen; i < width; i++) ck_putchar(' ');
            break;
        }
        case 'x':
        case 'X': {
            u64 val = is_long ? va_arg(ap, unsigned long long) : (u64)va_arg(ap, unsigned int);
            blen = utoa_hex(val, buf, (*fmt == 'X'));
            if (!left_align)
                for (int i = blen; i < width; i++) ck_putchar(pad_ch);
            ck_puts(buf);
            if (left_align)
                for (int i = blen; i < width; i++) ck_putchar(' ');
            break;
        }
        case 'p': {
            u64 val = (u64)(uintptr_t)va_arg(ap, void *);
            ck_puts("0x");
            blen = utoa_hex(val, buf, 0);
            /* pad pointers to 16 hex digits */
            for (int i = blen; i < 16; i++) ck_putchar('0');
            ck_puts(buf);
            break;
        }
        case '%':
            ck_putchar('%');
            break;
        case '\0':
            goto done;
        default:
            ck_putchar('%');
            ck_putchar(*fmt);
            break;
        }
        fmt++;
    }
done:
    va_end(ap);
}

/* ── Panic ──────────────────────────────────────────────────────────── */
void ck_panic(const char *msg)
{
    ck_printk("\n[PANIC] %s\n", msg);
    for (;;)
        __asm__ __volatile__("cli; hlt");
}
