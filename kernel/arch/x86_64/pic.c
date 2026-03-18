/*
 * kernel/arch/x86_64/pic.c
 *
 * 8259A Programmable Interrupt Controller driver.
 * Remaps IRQ 0-7  to vectors 32-39
 *        IRQ 8-15 to vectors 40-47
 * and masks all lines (drivers unmask what they need).
 */
#include <ck/io.h>
#include <ck/types.h>

/* PIC I/O ports */
#define PIC1_CMD  0x20
#define PIC1_DATA 0x21
#define PIC2_CMD  0xa0
#define PIC2_DATA 0xa1

/* Initialisation Command Words */
#define PIC_ICW1_INIT 0x10
#define PIC_ICW1_ICW4 0x01
#define PIC_ICW4_8086 0x01

/* IRQ base after remapping */
#define PIC1_IRQ_BASE 32
#define PIC2_IRQ_BASE 40

void pic_init(void)
{
    /* Save current masks */
    u8 m1 = inb(PIC1_DATA);
    u8 m2 = inb(PIC2_DATA);

    /* ICW1: start initialisation sequence */
    outb(PIC1_CMD,  PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_delay();
    outb(PIC2_CMD,  PIC_ICW1_INIT | PIC_ICW1_ICW4);
    io_delay();

    /* ICW2: vector offsets */
    outb(PIC1_DATA, PIC1_IRQ_BASE);
    io_delay();
    outb(PIC2_DATA, PIC2_IRQ_BASE);
    io_delay();

    /* ICW3: cascade wiring (master: IRQ2 → slave; slave: cascade ID 2) */
    outb(PIC1_DATA, 0x04);
    io_delay();
    outb(PIC2_DATA, 0x02);
    io_delay();

    /* ICW4: 8086/88 mode */
    outb(PIC1_DATA, PIC_ICW4_8086);
    io_delay();
    outb(PIC2_DATA, PIC_ICW4_8086);
    io_delay();

    /* Restore saved masks (mask all lines initially) */
    outb(PIC1_DATA, m1);
    outb(PIC2_DATA, m2);
}

/* Mask (disable) an IRQ line */
void pic_mask_irq(int irq)
{
    u16 port;
    u8  val;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    val = inb(port) | (u8)(1 << irq);
    outb(port, val);
}

/* Unmask (enable) an IRQ line */
void pic_unmask_irq(int irq)
{
    u16 port;
    u8  val;

    if (irq < 8) {
        port = PIC1_DATA;
    } else {
        port = PIC2_DATA;
        irq -= 8;
    }
    val = inb(port) & (u8)~(1 << irq);
    outb(port, val);
}

/* Mask all IRQ lines */
void pic_mask_all(void)
{
    outb(PIC1_DATA, 0xff);
    outb(PIC2_DATA, 0xff);
}
