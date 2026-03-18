/*
 * kernel/arch/x86_64/keyboard.c
 *
 * PS/2 keyboard driver (IRQ1, port 0x60).
 * Translates scan codes (set 1) to ASCII and stores them in a ring buffer.
 */
#include <ck/io.h>
#include <ck/types.h>
#include <ck/kernel.h>

#define PS2_DATA 0x60
#define PS2_STAT 0x64

#define KB_BUF_SIZE 128

static char   kb_buf[KB_BUF_SIZE];
static u32    kb_head = 0;
static u32    kb_tail = 0;

/* Minimal scan-code set 1 → ASCII (lowercase, no modifiers) */
static const char sc_to_ascii[128] = {
    0,   0,   '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-', '=',  '\b', '\t',
    'q', 'w', 'e', 'r', 't', 'y', 'u', 'i', 'o', 'p', '[', ']', '\n', 0,   'a',  's',
    'd', 'f', 'g', 'h', 'j', 'k', 'l', ';', '\'','`',  0,  '\\','z', 'x',  'c',  'v',
    'b', 'n', 'm', ',', '.', '/',  0,   '*',  0,  ' ',  0,   0,   0,   0,    0,    0,
    0,   0,   0,   0,   0,   0,   0,   '7', '8', '9', '-', '4', '5', '6',  '+',  '1',
    '2', '3', '0', '.', 0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,
    0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,   0,    0,    0,
};

static void kb_buf_push(char c)
{
    u32 next = (kb_head + 1) % KB_BUF_SIZE;
    if (next != kb_tail) {
        kb_buf[kb_head] = c;
        kb_head = next;
    }
}

/* Returns 0 if buffer empty */
int keyboard_getchar(void)
{
    if (kb_head == kb_tail)
        return 0;
    char c = kb_buf[kb_tail];
    kb_tail = (kb_tail + 1) % KB_BUF_SIZE;
    return (unsigned char)c;
}

/* IRQ1 handler */
void keyboard_irq_handler(void)
{
    u8  scan = inb(PS2_DATA);

    /* Ignore key-release events (bit 7 set) */
    if (scan & 0x80)
        return;

    if (scan < 128) {
        char c = sc_to_ascii[scan];
        if (c)
            kb_buf_push(c);
    }
}

void keyboard_init(void)
{
    /* Nothing to program for basic PS/2 in emulators */
}
