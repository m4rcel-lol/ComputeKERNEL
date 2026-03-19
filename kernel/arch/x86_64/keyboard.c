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

/* Scan codes for modifier keys */
#define SC_LSHIFT  0x2A
#define SC_RSHIFT  0x36
#define SC_CAPS    0x3A

/* Extended (E0-prefixed) scan codes for arrow keys */
#define SC_EXT_UP    0x48
#define SC_EXT_DOWN  0x50
#define SC_EXT_LEFT  0x4B
#define SC_EXT_RIGHT 0x4D

/* Special key codes pushed into the ring buffer (non-printable range) */
#define KEY_UP    0x10
#define KEY_DOWN  0x11
#define KEY_LEFT  0x12
#define KEY_RIGHT 0x13

#define KB_BUF_SIZE 128

static char   kb_buf[KB_BUF_SIZE];
static u32    kb_head = 0;
static u32    kb_tail = 0;
static int    kb_shift_l = 0; /* non-zero while Left Shift is held */
static int    kb_shift_r = 0; /* non-zero while Right Shift is held */
static int    kb_caps    = 0; /* Caps Lock toggle */
static int    kb_e0      = 0; /* non-zero after receiving 0xE0 prefix */

/* Scan-code set 1 → ASCII (unshifted) */
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

/* Scan-code set 1 → ASCII (shifted) */
static const char sc_to_ascii_shifted[128] = {
    0,   0,   '!', '@', '#', '$', '%', '^', '&', '*', '(', ')', '_', '+',  '\b', '\t',
    'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', '{', '}', '\n', 0,   'A',  'S',
    'D', 'F', 'G', 'H', 'J', 'K', 'L', ':', '"', '~',  0,  '|', 'Z', 'X',  'C',  'V',
    'B', 'N', 'M', '<', '>', '?',  0,   '*',  0,  ' ',  0,   0,   0,   0,    0,    0,
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
    u8 scan = inb(PS2_DATA);

    /* Extended key prefix: next byte is an extended scan code */
    if (scan == 0xE0) {
        kb_e0 = 1;
        return;
    }

    int release = (scan & 0x80) != 0;
    u8  code    = scan & 0x7F;

    if (kb_e0) {
        kb_e0 = 0;
        /* Handle extended arrow keys on press only */
        if (!release) {
            switch (code) {
            case SC_EXT_UP:    kb_buf_push((char)KEY_UP);    break;
            case SC_EXT_DOWN:  kb_buf_push((char)KEY_DOWN);  break;
            case SC_EXT_LEFT:  kb_buf_push((char)KEY_LEFT);  break;
            case SC_EXT_RIGHT: kb_buf_push((char)KEY_RIGHT); break;
            default: break;
            }
        }
        return;
    }

    /* Track Shift keys separately so releasing one doesn't clear both */
    if (code == SC_LSHIFT) { kb_shift_l = release ? 0 : 1; return; }
    if (code == SC_RSHIFT) { kb_shift_r = release ? 0 : 1; return; }

    /* Toggle Caps Lock on press */
    if (code == SC_CAPS && !release) {
        kb_caps ^= 1;
        return;
    }

    /* Ignore other key-release events */
    if (release)
        return;

    if (code < 128) {
        char c;
        if (kb_shift_l || kb_shift_r) {
            c = sc_to_ascii_shifted[code];
        } else {
            c = sc_to_ascii[code];
            /* Apply Caps Lock to letters only */
            if (kb_caps && c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
        }
        if (c)
            kb_buf_push(c);
    }
}

void keyboard_init(void)
{
    /* Nothing to program for basic PS/2 in emulators */
}
