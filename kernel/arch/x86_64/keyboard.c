/*
 * kernel/arch/x86_64/keyboard.c
 *
 * PS/2 keyboard driver (IRQ1, port 0x60).
 * Translates scan codes (set 1) to ASCII/CP437 and stores them in a ring
 * buffer.  Supports multiple keyboard layouts and Ctrl+C.
 */
#include <ck/io.h>
#include <ck/types.h>
#include <ck/kernel.h>
#include <ck/string.h>

#define PS2_DATA 0x60
#define PS2_STAT 0x64

/* Scan codes for modifier keys */
#define SC_LSHIFT  0x2A
#define SC_RSHIFT  0x36
#define SC_CAPS    0x3A
#define SC_LCTRL   0x1D   /* also Right Ctrl make code when E0-prefixed */

/* Extended (E0-prefixed) scan codes for arrow keys and right modifiers */
#define SC_EXT_UP    0x48
#define SC_EXT_DOWN  0x50
#define SC_EXT_LEFT  0x4B
#define SC_EXT_RIGHT 0x4D

/* Special key codes pushed into the ring buffer (non-printable range) */
#define KEY_UP     0x10
#define KEY_DOWN   0x11
#define KEY_LEFT   0x12
#define KEY_RIGHT  0x13
#define KEY_CTRL_C 0x03   /* ETX – Ctrl+C */

/* CP437 extended characters used in non-US layouts */
#define CP437_U_UMLAUT   '\x81'   /* ü */
#define CP437_E_ACUTE    '\x82'   /* é */
#define CP437_A_UMLAUT   '\x84'   /* ä */
#define CP437_A_GRAVE    '\x85'   /* à */
#define CP437_C_CEDIL    '\x87'   /* ç */
#define CP437_E_GRAVE    '\x8A'   /* è */
#define CP437_A_UML_CAP  '\x8E'   /* Ä */
#define CP437_O_UMLAUT   '\x94'   /* ö */
#define CP437_U_GRAVE    '\x97'   /* ù */
#define CP437_O_UML_CAP  '\x99'   /* Ö */
#define CP437_U_UML_CAP  '\x9A'   /* Ü */
#define CP437_POUND      '\x9C'   /* £ */
#define CP437_SHARP_S    '\xE1'   /* ß */
#define CP437_MU         '\xE6'   /* μ */
#define CP437_DEGREE     '\xF8'   /* ° */

#define KB_BUF_SIZE 128

/* ── Keyboard layout ──────────────────────────────────────────────────── */

struct kb_sublayout {
    const char *name;
    const char *desc;
};

struct kb_layout {
    const char          *name;
    const char          *desc;
    char                 sc_lower[128];
    char                 sc_upper[128];
    int                  num_sublayouts;
    struct kb_sublayout  sublayouts[4];
};

/* ── US English (QWERTY) ─────────────────────────────────────────────── */
static const struct kb_layout layout_us = {
    .name = "us",
    .desc = "US English (QWERTY)",
    .sc_lower = {
        /* 0x00 */ 0,    0,    '1',  '2',  '3',  '4',  '5',  '6',
        /* 0x08 */ '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
        /* 0x10 */ 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
        /* 0x18 */ 'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
        /* 0x20 */ 'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
        /* 0x28 */ '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
        /* 0x30 */ 'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
        /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
        /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    '7',
        /* 0x48 */ '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
        /* 0x50 */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
        /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x60 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
    },
    .sc_upper = {
        /* 0x00 */ 0,    0,    '!',  '@',  '#',  '$',  '%',  '^',
        /* 0x08 */ '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
        /* 0x10 */ 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
        /* 0x18 */ 'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
        /* 0x20 */ 'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
        /* 0x28 */ '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
        /* 0x30 */ 'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
        /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
        /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    '7',
        /* 0x48 */ '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
        /* 0x50 */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
        /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x60 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
    },
    .num_sublayouts = 1,
    .sublayouts = {
        { "intl", "International (dead keys for accented characters)" },
    },
};

/* ── German (QWERTZ) ─────────────────────────────────────────────────── */
static const struct kb_layout layout_de = {
    .name = "de",
    .desc = "German (QWERTZ)",
    .sc_lower = {
        /* 0x00 */ 0,    0,    '1',  '2',  '3',  '4',  '5',  '6',
        /* 0x08 */ '7',  '8',  '9',  '0',  CP437_SHARP_S, 0, '\b', '\t',
        /* 0x10 */ 'q',  'w',  'e',  'r',  't',  'z',  'u',  'i',
        /* 0x18 */ 'o',  'p',  CP437_U_UMLAUT, '+', '\n', 0, 'a',  's',
        /* 0x20 */ 'd',  'f',  'g',  'h',  'j',  'k',  'l',  CP437_O_UMLAUT,
        /* 0x28 */ CP437_A_UMLAUT, '^', 0, '#',  'y',  'x',  'c',  'v',
        /* 0x30 */ 'b',  'n',  'm',  ',',  '.',  '-',  0,    '*',
        /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
        /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    '7',
        /* 0x48 */ '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
        /* 0x50 */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
        /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x60 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
    },
    .sc_upper = {
        /* 0x00 */ 0,    0,    '!',  '"',  0,    '$',  '%',  '&',
        /* 0x08 */ '/',  '(',  ')',  '=',  '?',  '`',  '\b', '\t',
        /* 0x10 */ 'Q',  'W',  'E',  'R',  'T',  'Z',  'U',  'I',
        /* 0x18 */ 'O',  'P',  CP437_U_UML_CAP, '*', '\n', 0, 'A',  'S',
        /* 0x20 */ 'D',  'F',  'G',  'H',  'J',  'K',  'L',  CP437_O_UML_CAP,
        /* 0x28 */ CP437_A_UML_CAP, CP437_DEGREE, 0, '\'', 'Y', 'X', 'C', 'V',
        /* 0x30 */ 'B',  'N',  'M',  ';',  ':',  '_',  0,    '*',
        /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
        /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    '7',
        /* 0x48 */ '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
        /* 0x50 */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
        /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x60 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
    },
    .num_sublayouts = 2,
    .sublayouts = {
        { "nodeadkeys", "No dead keys (^, ` produce characters directly)" },
        { "neo",        "Neo2 ergonomic layout variant"                   },
    },
};

/* ── French (AZERTY) ─────────────────────────────────────────────────── */
static const struct kb_layout layout_fr = {
    .name = "fr",
    .desc = "French (AZERTY)",
    .sc_lower = {
        /* 0x00 */ 0,    0,    '&',  CP437_E_ACUTE, '"',  '\'', '(',  '-',
        /* 0x08 */ CP437_E_GRAVE, '_', CP437_C_CEDIL, CP437_A_GRAVE, ')', '=', '\b', '\t',
        /* 0x10 */ 'a',  'z',  'e',  'r',  't',  'y',  'u',  'i',
        /* 0x18 */ 'o',  'p',  '^',  '$',  '\n', 0,    'q',  's',
        /* 0x20 */ 'd',  'f',  'g',  'h',  'j',  'k',  'l',  'm',
        /* 0x28 */ CP437_U_GRAVE, 0,   0,    '*',  'w',  'x',  'c',  'v',
        /* 0x30 */ 'b',  'n',  ',',  ';',  ':',  '!',  0,    '*',
        /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
        /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    '7',
        /* 0x48 */ '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
        /* 0x50 */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
        /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x60 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
    },
    .sc_upper = {
        /* 0x00 */ 0,    0,    '1',  '2',  '3',  '4',  '5',  '6',
        /* 0x08 */ '7',  '8',  '9',  '0',  CP437_DEGREE, '+', '\b', '\t',
        /* 0x10 */ 'A',  'Z',  'E',  'R',  'T',  'Y',  'U',  'I',
        /* 0x18 */ 'O',  'P',  0,    CP437_POUND, '\n', 0,  'Q',  'S',
        /* 0x20 */ 'D',  'F',  'G',  'H',  'J',  'K',  'L',  'M',
        /* 0x28 */ '%',  0,    0,    CP437_MU, 'W', 'X',  'C',  'V',
        /* 0x30 */ 'B',  'N',  '?',  '.',  '/',  0,    0,    '*',
        /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
        /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    '7',
        /* 0x48 */ '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
        /* 0x50 */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
        /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x60 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
    },
    .num_sublayouts = 1,
    .sublayouts = {
        { "oss", "Open Source (adds extra accented characters)" },
    },
};

/* ── UK English (QWERTY) ─────────────────────────────────────────────── */
static const struct kb_layout layout_uk = {
    .name = "uk",
    .desc = "UK English (QWERTY)",
    .sc_lower = {
        /* 0x00 */ 0,    0,    '1',  '2',  '3',  '4',  '5',  '6',
        /* 0x08 */ '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
        /* 0x10 */ 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
        /* 0x18 */ 'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
        /* 0x20 */ 'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
        /* 0x28 */ '\'', '`',  0,    '#',  'z',  'x',  'c',  'v',
        /* 0x30 */ 'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
        /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
        /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    '7',
        /* 0x48 */ '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
        /* 0x50 */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
        /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x60 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
    },
    .sc_upper = {
        /* 0x00 */ 0,    0,    '!',  '"',  CP437_POUND, '$', '%',  '^',
        /* 0x08 */ '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
        /* 0x10 */ 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
        /* 0x18 */ 'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
        /* 0x20 */ 'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
        /* 0x28 */ '@',  '~',  0,    '~',  'Z',  'X',  'C',  'V',
        /* 0x30 */ 'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
        /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
        /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    '7',
        /* 0x48 */ '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
        /* 0x50 */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
        /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x60 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
    },
    .num_sublayouts = 0,
    .sublayouts = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 0, 0 } },
};

/* ── Polish Programmer (QWERTY) ──────────────────────────────────────── */
static const struct kb_layout layout_pl = {
    .name = "pl",
    .desc = "Polish Programmer (QWERTY)",
    .sc_lower = {
        /* 0x00 */ 0,    0,    '1',  '2',  '3',  '4',  '5',  '6',
        /* 0x08 */ '7',  '8',  '9',  '0',  '-',  '=',  '\b', '\t',
        /* 0x10 */ 'q',  'w',  'e',  'r',  't',  'y',  'u',  'i',
        /* 0x18 */ 'o',  'p',  '[',  ']',  '\n', 0,    'a',  's',
        /* 0x20 */ 'd',  'f',  'g',  'h',  'j',  'k',  'l',  ';',
        /* 0x28 */ '\'', '`',  0,    '\\', 'z',  'x',  'c',  'v',
        /* 0x30 */ 'b',  'n',  'm',  ',',  '.',  '/',  0,    '*',
        /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
        /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    '7',
        /* 0x48 */ '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
        /* 0x50 */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
        /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x60 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
    },
    .sc_upper = {
        /* 0x00 */ 0,    0,    '!',  '@',  '#',  '$',  '%',  '^',
        /* 0x08 */ '&',  '*',  '(',  ')',  '_',  '+',  '\b', '\t',
        /* 0x10 */ 'Q',  'W',  'E',  'R',  'T',  'Y',  'U',  'I',
        /* 0x18 */ 'O',  'P',  '{',  '}',  '\n', 0,    'A',  'S',
        /* 0x20 */ 'D',  'F',  'G',  'H',  'J',  'K',  'L',  ':',
        /* 0x28 */ '"',  '~',  0,    '|',  'Z',  'X',  'C',  'V',
        /* 0x30 */ 'B',  'N',  'M',  '<',  '>',  '?',  0,    '*',
        /* 0x38 */ 0,    ' ',  0,    0,    0,    0,    0,    0,
        /* 0x40 */ 0,    0,    0,    0,    0,    0,    0,    '7',
        /* 0x48 */ '8',  '9',  '-',  '4',  '5',  '6',  '+',  '1',
        /* 0x50 */ '2',  '3',  '0',  '.',  0,    0,    0,    0,
        /* 0x58 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x60 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x68 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x70 */ 0,    0,    0,    0,    0,    0,    0,    0,
        /* 0x78 */ 0,    0,    0,    0,    0,    0,    0,    0,
    },
    .num_sublayouts = 2,
    .sublayouts = {
        { "qwerty",  "Standard QWERTY base (AltGr adds Polish diacritics)" },
        { "dvorak",  "Polish Dvorak variant"                                },
    },
};

/* ── Layout registry ─────────────────────────────────────────────────── */

static const struct kb_layout * const all_layouts[] = {
    &layout_us, &layout_de, &layout_fr, &layout_uk, &layout_pl,
};
#define KB_NUM_LAYOUTS 5

/* Active layout and sublayout */
static const struct kb_layout *kb_cur_layout    = &layout_us;
static int                     kb_cur_sublayout = -1; /* -1 = default */

/* ── Ring buffer ─────────────────────────────────────────────────────── */

static char kb_buf[KB_BUF_SIZE];
static u32  kb_head = 0;
static u32  kb_tail = 0;

/* Modifier state */
static int kb_shift_l = 0;
static int kb_shift_r = 0;
static int kb_caps    = 0;
static int kb_ctrl_l  = 0;   /* Left Ctrl held */
static int kb_ctrl_r  = 0;   /* Right Ctrl held (E0-prefixed) */
static int kb_e0      = 0;   /* non-zero after 0xE0 prefix */

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

/* ── Layout API ──────────────────────────────────────────────────────── */

int keyboard_get_layout_count(void)
{
    return KB_NUM_LAYOUTS;
}

const char *keyboard_get_layout_name_at(int i)
{
    if (i < 0 || i >= KB_NUM_LAYOUTS) return 0;
    return all_layouts[i]->name;
}

const char *keyboard_get_layout_desc_at(int i)
{
    if (i < 0 || i >= KB_NUM_LAYOUTS) return 0;
    return all_layouts[i]->desc;
}

int keyboard_get_sublayout_count_at(int layout_i)
{
    if (layout_i < 0 || layout_i >= KB_NUM_LAYOUTS) return 0;
    return all_layouts[layout_i]->num_sublayouts;
}

const char *keyboard_get_sublayout_name_at(int layout_i, int sub_i)
{
    if (layout_i < 0 || layout_i >= KB_NUM_LAYOUTS) return 0;
    const struct kb_layout *l = all_layouts[layout_i];
    if (sub_i < 0 || sub_i >= l->num_sublayouts) return 0;
    return l->sublayouts[sub_i].name;
}

const char *keyboard_get_sublayout_desc_at(int layout_i, int sub_i)
{
    if (layout_i < 0 || layout_i >= KB_NUM_LAYOUTS) return 0;
    const struct kb_layout *l = all_layouts[layout_i];
    if (sub_i < 0 || sub_i >= l->num_sublayouts) return 0;
    return l->sublayouts[sub_i].desc;
}

/* Returns the name of the currently active layout */
const char *keyboard_get_layout_name(void)
{
    return kb_cur_layout->name;
}

/* Returns the current sublayout index (-1 = default) */
int keyboard_get_sublayout_idx(void)
{
    return kb_cur_sublayout;
}

/* Set layout by name; returns 0 on success, -1 if not found */
int keyboard_set_layout(const char *name)
{
    if (!name) return -1;
    for (int i = 0; i < KB_NUM_LAYOUTS; i++) {
        if (strcmp(all_layouts[i]->name, name) == 0) {
            kb_cur_layout    = all_layouts[i];
            kb_cur_sublayout = -1;
            return 0;
        }
    }
    return -1;
}

/* Set sublayout index for the current layout */
void keyboard_set_sublayout(int sub_i)
{
    if (sub_i >= -1 && sub_i < kb_cur_layout->num_sublayouts)
        kb_cur_sublayout = sub_i;
}

/* ── IRQ1 handler ────────────────────────────────────────────────────── */

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
        if (code == SC_LCTRL) {
            /* Right Ctrl (E0-prefixed) */
            kb_ctrl_r = release ? 0 : 1;
            return;
        }
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

    /* Left Ctrl */
    if (code == SC_LCTRL) { kb_ctrl_l = release ? 0 : 1; return; }

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
            c = kb_cur_layout->sc_upper[code];
        } else {
            c = kb_cur_layout->sc_lower[code];
            /* Apply Caps Lock to ASCII letters only */
            if (kb_caps && c >= 'a' && c <= 'z')
                c = (char)(c - 'a' + 'A');
        }

        /* Ctrl+C: generate ETX regardless of layout */
        if ((kb_ctrl_l || kb_ctrl_r) && (c == 'c' || c == 'C')) {
            kb_buf_push((char)KEY_CTRL_C);
            return;
        }

        if (c)
            kb_buf_push(c);
    }
}

void keyboard_init(void)
{
    /* Nothing to program for basic PS/2 in emulators */
}
