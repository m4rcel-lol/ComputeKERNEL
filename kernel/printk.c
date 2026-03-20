#include <ck/kernel.h>
#include <ck/io.h>
#include <ck/types.h>
#include <ck/string.h>
#include <stdarg.h>

/* ── VGA text console ───────────────────────────────────────────────── */
#define VGA_BASE        ((volatile unsigned short *)0xb8000)
#define VGA_COLS        80
#define VGA_ROWS        50
#define VGA_COLOR_WHITE 0x0f   /* white on black (default) */
#define SCROLLBACK_LINES 200
#define VGA_MAX_SCAN_MASK 0xe0U
#define VGA_SCANLINE_8    0x07U

static int col = 0, row = 0;
static u8  vga_color = VGA_COLOR_WHITE;
static unsigned short scrollback[SCROLLBACK_LINES][VGA_COLS];
static unsigned short live_screen[VGA_ROWS][VGA_COLS];
static u32 scrollback_used = 0;
static u32 scroll_view = 0;
static int live_saved = 0;

static void vga_update_cursor(void);
static void vga_enable_80x50(void);

static void vga_enable_80x50(void)
{
    /*
     * Switch from 80x25 (16-scanline font) to 80x50 (8-scanline font)
     * by halving the maximum scan line height.
     */
    outb(0x3d4, 0x09);
    u8 max_scan = inb(0x3d5);
    /* Preserve upper control bits, set max scanline to 7 (8-scanline font). */
    max_scan = (max_scan & VGA_MAX_SCAN_MASK) | VGA_SCANLINE_8;
    outb(0x3d5, max_scan);
}

static void scrollback_push_line(const volatile unsigned short *line)
{
    if (scrollback_used < SCROLLBACK_LINES) {
        for (int c = 0; c < VGA_COLS; c++)
            scrollback[scrollback_used][c] = line[c];
        scrollback_used++;
        return;
    }
    for (u32 r = 1; r < SCROLLBACK_LINES; r++)
        for (int c = 0; c < VGA_COLS; c++)
            scrollback[r - 1][c] = scrollback[r][c];
    for (int c = 0; c < VGA_COLS; c++)
        scrollback[SCROLLBACK_LINES - 1][c] = line[c];
}

static void render_scrollback_view(void)
{
    volatile unsigned short *vga = VGA_BASE;
    if (scroll_view == 0)
        return;

    u32 first = (scrollback_used > scroll_view) ? (scrollback_used - scroll_view) : 0;
    for (int r = 0; r < VGA_ROWS; r++) {
        u32 src = first + (u32)r;
        for (int c = 0; c < VGA_COLS; c++) {
            if (src < scrollback_used)
                vga[r * VGA_COLS + c] = scrollback[src][c];
            else
                vga[r * VGA_COLS + c] = ((unsigned short)VGA_COLOR_WHITE << 8) | ' ';
        }
    }
    vga_update_cursor();
}

static void save_live_screen(void)
{
    volatile unsigned short *vga = VGA_BASE;
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            live_screen[r][c] = vga[r * VGA_COLS + c];
    live_saved = 1;
}

static void restore_live_screen(void)
{
    if (!live_saved)
        return;
    volatile unsigned short *vga = VGA_BASE;
    for (int r = 0; r < VGA_ROWS; r++)
        for (int c = 0; c < VGA_COLS; c++)
            vga[r * VGA_COLS + c] = live_screen[r][c];
    live_saved = 0;
}

static void vga_update_cursor(void)
{
    if (scroll_view > 0)
        return;
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

    scrollback_push_line(vga);
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
    vga_enable_80x50();

    for (i = 0; i < VGA_COLS * VGA_ROWS; i++)
        vga[i] = ((unsigned short)VGA_COLOR_WHITE << 8) | ' ';
    col = 0;
    row = 0;
    vga_color = VGA_COLOR_WHITE;
    scrollback_used = 0;
    scroll_view = 0;
    live_saved = 0;
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
    if (scroll_view > 0) {
        scroll_view = 0;
        restore_live_screen();
    }

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

void ck_console_scroll_up(u32 lines)
{
    if (lines == 0 || scrollback_used == 0)
        return;
    if (scroll_view == 0)
        save_live_screen();
    u32 max_view = scrollback_used;
    if (scroll_view + lines > max_view)
        scroll_view = max_view;
    else
        scroll_view += lines;
    render_scrollback_view();
}

void ck_console_scroll_down(u32 lines)
{
    if (lines == 0 || scroll_view == 0)
        return;
    if (lines >= scroll_view)
        scroll_view = 0;
    else
        scroll_view -= lines;
    if (scroll_view > 0)
        render_scrollback_view();
    else {
        restore_live_screen();
        vga_update_cursor();
    }
}

void ck_console_scroll_reset(void)
{
    if (scroll_view == 0)
        return;
    scroll_view = 0;
    restore_live_screen();
    vga_update_cursor();
}

/* ── Panic ──────────────────────────────────────────────────────────── */
void ck_panic(const char *msg)
{
    ck_printk("\n[PANIC] %s\n", msg);
    for (;;)
        __asm__ __volatile__("cli; hlt");
}
