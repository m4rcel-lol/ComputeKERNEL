/*
 * kernel/arch/x86_64/mouse.c
 *
 * Minimal PS/2 mouse driver (IRQ12, port 0x60/0x64).
 * Captures movement/button packets into a small ring buffer for shell use.
 */
#include <ck/io.h>
#include <ck/types.h>
#include <ck/mouse.h>

#define PS2_DATA 0x60
#define PS2_STAT 0x64
#define PS2_CMD  0x64

#define MOUSE_BUF_SIZE 64

static struct mouse_event mouse_buf[MOUSE_BUF_SIZE];
static u32 mouse_head = 0;
static u32 mouse_tail = 0;

static int mouse_available = 0;
static int mouse_x = 40;
static int mouse_y = 12;
static u8  mouse_buttons = 0;

static u8 packet[3];
static int packet_index = 0;

static int ps2_wait_write(void)
{
    for (u32 i = 0; i < 100000; i++) {
        if ((inb(PS2_STAT) & 0x02) == 0)
            return 0;
        io_delay();
    }
    return -1;
}

static int ps2_wait_read(void)
{
    for (u32 i = 0; i < 100000; i++) {
        if (inb(PS2_STAT) & 0x01)
            return 0;
        io_delay();
    }
    return -1;
}

static int mouse_write(u8 value)
{
    if (ps2_wait_write() < 0)
        return -1;
    outb(PS2_CMD, 0xD4);
    if (ps2_wait_write() < 0)
        return -1;
    outb(PS2_DATA, value);
    if (ps2_wait_read() < 0)
        return -1;
    return inb(PS2_DATA) == 0xFA ? 0 : -1;
}

static void mouse_buf_push(const struct mouse_event *ev)
{
    u32 next = (mouse_head + 1) % MOUSE_BUF_SIZE;
    if (next != mouse_tail) {
        mouse_buf[mouse_head] = *ev;
        mouse_head = next;
    }
}

int mouse_getevent(struct mouse_event *ev)
{
    if (!ev || mouse_head == mouse_tail)
        return 0;
    *ev = mouse_buf[mouse_tail];
    mouse_tail = (mouse_tail + 1) % MOUSE_BUF_SIZE;
    return 1;
}

int mouse_is_available(void)
{
    return mouse_available;
}

void mouse_get_position(int *x, int *y, u8 *buttons)
{
    if (x) *x = mouse_x;
    if (y) *y = mouse_y;
    if (buttons) *buttons = mouse_buttons;
}

void mouse_irq_handler(void)
{
    u8 b = inb(PS2_DATA);
    if (!mouse_available)
        return;

    if (packet_index == 0 && (b & 0x08) == 0)
        return;

    packet[packet_index++] = b;
    if (packet_index < 3)
        return;
    packet_index = 0;

    struct mouse_event ev;
    ev.buttons = packet[0] & 0x07;
    ev.dx = (s8)packet[1];
    ev.dy = (s8)packet[2];
    mouse_buttons = ev.buttons;

    mouse_x += (int)ev.dx;
    mouse_y -= (int)ev.dy;
    if (mouse_x < 0) mouse_x = 0;
    if (mouse_x > 79) mouse_x = 79;
    if (mouse_y < 0) mouse_y = 0;
    if (mouse_y > 24) mouse_y = 24;

    if (ev.dx != 0 || ev.dy != 0 || ev.buttons != 0)
        mouse_buf_push(&ev);
}

void mouse_init(void)
{
    mouse_available = 0;
    packet_index = 0;
    mouse_head = mouse_tail = 0;
    mouse_buttons = 0;

    if (ps2_wait_write() < 0)
        return;
    outb(PS2_CMD, 0xA8); /* enable aux port */

    if (ps2_wait_write() < 0)
        return;
    outb(PS2_CMD, 0x20); /* read controller config byte */
    if (ps2_wait_read() < 0)
        return;
    u8 status = inb(PS2_DATA);
    status |= 0x02; /* enable IRQ12 */
    if (ps2_wait_write() < 0)
        return;
    outb(PS2_CMD, 0x60); /* write controller config byte */
    if (ps2_wait_write() < 0)
        return;
    outb(PS2_DATA, status);

    if (mouse_write(0xF6) < 0) /* defaults */
        return;
    if (mouse_write(0xF4) < 0) /* enable streaming */
        return;

    mouse_available = 1;
}
