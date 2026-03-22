#include <ck/kernel.h>
#include <ck/io.h>
#include <ck/types.h>
#include <ck/string.h>
#include <stdarg.h>
#include <limine.h>

/* Forward declaration to avoid circular include */
void serial_putchar(char c);

/* Limine requests defined in kernel/init/main.c */
extern struct limine_framebuffer_request limine_framebuffer_request;
extern struct limine_hhdm_request limine_hhdm_request;

/* ── VGA text console (fallback) ────────────────────────────────────── */
#define VGA_BASE        ((volatile unsigned short *)0xb8000)
#define VGA_COLS        80
#define VGA_ROWS        25
#define VGA_COLOR_WHITE 0x0f   /* white on black (default) */
#define SCROLLBACK_LINES 200

static u32 col = 0, row = 0;
static u8  vga_color = VGA_COLOR_WHITE;
static u32 display_width = VGA_COLS;
static u32 display_height = VGA_ROWS;
static unsigned short scrollback[SCROLLBACK_LINES][VGA_COLS];
static u32 scrollback_used = 0;
static u32 scroll_view = 0;

static int has_framebuffer = 0;
static struct limine_framebuffer *fb = NULL;

extern const u8 ck_font_8x16[95][16];

/* Simple 8x16 font rendering */
static void fb_putchar(int x, int y, char c, u32 color) {
    if (!fb || c < 32 || c > 126) return;
    u32 *fb_ptr = (u32 *)fb->address;
    u64 row_size = fb->pitch / 4;
    const u8 *glyph = ck_font_8x16[c - 32];

    for (int j = 0; j < 16; j++) {
        for (int i = 0; i < 8; i++) {
            if (glyph[j] & (1 << (7 - i))) {
                fb_ptr[(y * 16 + j) * row_size + (x * 8 + i)] = color;
            } else {
                fb_ptr[(y * 16 + j) * row_size + (x * 8 + i)] = 0; /* Background black */
            }
        }
    }
}

static void vga_update_cursor(void)
{
    if (has_framebuffer || scroll_view > 0)
        return;
    u16 pos = (u16)(row * VGA_COLS + col);
    outb(0x3d4, 0x0e);
    outb(0x3d5, (u8)(pos >> 8));
    outb(0x3d4, 0x0f);
    outb(0x3d5, (u8)pos);
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

static void console_scroll(void)
{
    if (has_framebuffer) {
        /* Simple framebuffer scroll: move everything up by 16 lines */
        u32 *fb_ptr = (u32 *)fb->address;
        u64 row_size = fb->pitch / 4;
        for (u64 i = 0; i < (fb->height - 16) * row_size; i++) {
            fb_ptr[i] = fb_ptr[i + 16 * row_size];
        }
        /* Clear the last line */
        for (u64 i = (fb->height - 16) * row_size; i < fb->height * row_size; i++) {
            fb_ptr[i] = 0;
        }
    } else {
        volatile unsigned short *vga = VGA_BASE;
        scrollback_push_line(vga);
        for (int r = 0; r < VGA_ROWS - 1; r++)
            for (int c = 0; c < VGA_COLS; c++)
                vga[r * VGA_COLS + c] = vga[(r + 1) * VGA_COLS + c];

        for (int c = 0; c < VGA_COLS; c++)
            vga[(VGA_ROWS - 1) * VGA_COLS + c] = ((unsigned short)VGA_COLOR_WHITE << 8) | ' ';
    }
    row = display_height - 1;
    vga_update_cursor();
}

void ck_early_console_init(void)
{
    if (limine_framebuffer_request.response && limine_framebuffer_request.response->framebuffer_count > 0) {
        fb = limine_framebuffer_request.response->framebuffers[0];
        has_framebuffer = 1;
        display_width = fb->width / 8;
        display_height = fb->height / 16;
        /* Clear screen */
        u32 *fb_ptr = (u32 *)fb->address;
        for (u64 i = 0; i < fb->width * fb->height; i++)
            fb_ptr[i] = 0;
    } else {
        volatile unsigned short *vga = VGA_BASE;
        for (int i = 0; i < VGA_COLS * VGA_ROWS; i++)
            vga[i] = ((unsigned short)VGA_COLOR_WHITE << 8) | ' ';
        display_width = VGA_COLS;
        display_height = VGA_ROWS;
    }

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

void ck_display_get_resolution(u32 *width, u32 *height)
{
    if (has_framebuffer && fb) {
        if (width) *width = (u32)fb->width;
        if (height) *height = (u32)fb->height;
    } else {
        if (width) *width = VGA_COLS;
        if (height) *height = VGA_ROWS;
    }
}

int ck_display_set_resolution(u32 width, u32 height)
{
    (void)width;
    (void)height;
    /* Resolution is fixed by Limine at boot. 
       Return -1 to indicate it cannot be changed at runtime. */
    return -1;
}

void ck_putchar(char c)
{
    /* Mirror to serial port for debugging */
    serial_putchar(c);

    if (c == '\n') {
        col = 0;
        row++;
    } else if (c == '\r') {
        col = 0;
    } else if (c == '\b') {
        if (col > 0) col--;
        else if (row > 0) { row--; col = display_width - 1; }
        
        if (has_framebuffer) {
            /* Clear the character on FB */
            for (u32 j = 0; j < 16; j++)
                for (u32 i = 0; i < 8; i++)
                    ((u32 *)fb->address)[(row * 16 + j) * (fb->pitch / 4) + (col * 8 + i)] = 0;
        } else {
            VGA_BASE[row * VGA_COLS + col] = ((unsigned short)vga_color << 8) | ' ';
        }
        vga_update_cursor();
        return;
    } else if (c == '\t') {
        col = (col + 8) & ~7;
        if (col >= display_width) { col = 0; row++; }
    } else {
        if (has_framebuffer) {
            /* Convert VGA color to RGB (simplified) */
            u32 color = 0xFFFFFF; /* Default white */
            if (vga_color == CK_COLOR_RED) color = 0xFF0000;
            else if (vga_color == CK_COLOR_GREEN) color = 0x00FF00;
            else if (vga_color == CK_COLOR_BLUE) color = 0x0000FF;
            
            fb_putchar(col, row, c, color);
        } else {
            VGA_BASE[row * VGA_COLS + col] = ((unsigned short)vga_color << 8) | (unsigned char)c;
        }
        
        if (++col >= display_width) {
            col = 0;
            row++;
        }
    }

    if (row >= display_height)
        console_scroll();
    else
        vga_update_cursor();
}

void ck_puts(const char *s)
{
    while (*s)
        ck_putchar(*s++);
}

/* ── Full printf-style formatter ────────────────────────────────────── */

void ck_printk(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);

    while (*fmt) {
        if (*fmt == '%') {
            fmt++;
            int long_long = 0;
            int long_flag = 0;

            if (*fmt == 'l') {
                long_flag = 1;
                fmt++;
                if (*fmt == 'l') {
                    long_long = 1;
                    fmt++;
                }
            }

            char buf[32];
            switch (*fmt) {
            case 's': {
                const char *s = va_arg(args, const char *);
                ck_puts(s ? s : "(null)");
                break;
            }
            case 'c': {
                ck_putchar((char)va_arg(args, int));
                break;
            }
            case 'd': {
                if (long_long || long_flag) itoa_dec(va_arg(args, s64), buf);
                else itoa_dec(va_arg(args, int), buf);
                ck_puts(buf);
                break;
            }
            case 'u': {
                if (long_long || long_flag) utoa_dec(va_arg(args, u64), buf);
                else utoa_dec(va_arg(args, unsigned int), buf);
                ck_puts(buf);
                break;
            }
            case 'x':
            case 'X': {
                if (long_long || long_flag) utoa_hex(va_arg(args, u64), buf, (*fmt == 'X'));
                else utoa_hex(va_arg(args, unsigned int), buf, (*fmt == 'X'));
                ck_puts(buf);
                break;
            }
            case 'p': {
                ck_puts("0x");
                utoa_hex((u64)(uintptr_t)va_arg(args, void *), buf, 0);
                ck_puts(buf);
                break;
            }
            case '%': {
                ck_putchar('%');
                break;
            }
            default:
                ck_putchar('%');
                ck_putchar(*fmt);
                break;
            }
        } else {
            ck_putchar(*fmt);
        }
        fmt++;
    }

    va_end(args);
}
