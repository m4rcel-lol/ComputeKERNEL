/*
 * kernel/drivers/serial.c – NS16550-compatible UART driver (COM1)
 *
 * Provides an early serial console (115200 8N1) that mirrors VGA output
 * and is extremely useful for QEMU debugging (-serial stdio).
 */
#include <ck/kernel.h>
#include <ck/io.h>
#include <ck/types.h>

#define COM1_BASE 0x3f8

/* UART register offsets */
#define UART_RBR  0   /* Receive Buffer Register (read) */
#define UART_THR  0   /* Transmit Holding Register (write) */
#define UART_IER  1   /* Interrupt Enable Register */
#define UART_FCR  2   /* FIFO Control Register */
#define UART_LCR  3   /* Line Control Register */
#define UART_MCR  4   /* Modem Control Register */
#define UART_LSR  5   /* Line Status Register */
#define UART_MSR  6   /* Modem Status Register */
#define UART_SCR  7   /* Scratch Register */
#define UART_DLL  0   /* Divisor Latch Low  (when DLAB=1) */
#define UART_DLH  1   /* Divisor Latch High (when DLAB=1) */

#define LSR_THRE  0x20  /* Transmit Holding Register Empty */
#define LCR_DLAB  0x80  /* Divisor Latch Access Bit */
#define LCR_8N1   0x03  /* 8 data bits, no parity, 1 stop bit */
#define FCR_ENABLE 0x07 /* Enable FIFO, clear, 14-byte threshold */

static int serial_ready = 0;

void serial_init(void)
{
    /* Disable all UART interrupts */
    outb(COM1_BASE + UART_IER, 0x00);

    /* Set DLAB to access divisor latches */
    outb(COM1_BASE + UART_LCR, LCR_DLAB);
    /* Set divisor for 115200 baud (clock = 1.8432 MHz, divisor = 1) */
    outb(COM1_BASE + UART_DLL, 0x01);
    outb(COM1_BASE + UART_DLH, 0x00);

    /* 8 data bits, no parity, 1 stop bit (also clears DLAB) */
    outb(COM1_BASE + UART_LCR, LCR_8N1);

    /* Enable and clear FIFOs */
    outb(COM1_BASE + UART_FCR, FCR_ENABLE);

    /* Set RTS + DTR (needed by some hosts) */
    outb(COM1_BASE + UART_MCR, 0x03);

    /* Quick loopback test – if scratch register works, UART is present */
    outb(COM1_BASE + UART_SCR, 0xAE);
    if (inb(COM1_BASE + UART_SCR) == 0xAE)
        serial_ready = 1;
}

static void serial_wait_tx(void)
{
    while (!(inb(COM1_BASE + UART_LSR) & LSR_THRE))
        cpu_pause();
}

void serial_putchar(char c)
{
    if (!serial_ready)
        return;
    if (c == '\n') {
        serial_wait_tx();
        outb(COM1_BASE + UART_THR, '\r');
    }
    serial_wait_tx();
    outb(COM1_BASE + UART_THR, (u8)c);
}

void serial_puts(const char *s)
{
    while (*s)
        serial_putchar(*s++);
}
