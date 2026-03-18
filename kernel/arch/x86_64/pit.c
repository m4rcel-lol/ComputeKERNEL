/*
 * kernel/arch/x86_64/pit.c
 *
 * 8253/8254 Programmable Interval Timer.
 * Configures channel 0 for a periodic IRQ0 at ~100 Hz.
 */
#include <ck/io.h>
#include <ck/types.h>

/* PIT I/O ports */
#define PIT_CHANNEL0 0x40
#define PIT_COMMAND  0x43

/* PIT input clock in Hz */
#define PIT_BASE_HZ  1193182UL

/* Target frequency */
#define PIT_TARGET_HZ 100UL

/* Mode/Command: channel 0, lobyte/hibyte, mode 2 (rate generator) */
#define PIT_CMD_CHANNEL0    0x00
#define PIT_CMD_ACCESS_BOTH 0x30
#define PIT_CMD_MODE2       0x04

/* Global tick counter (incremented by IRQ0 handler) */
static volatile u64 pit_ticks = 0;

u64 pit_get_ticks(void)
{
    return pit_ticks;
}

void pit_tick(void)
{
    pit_ticks++;
}

/* Spin-wait for approximately n milliseconds (busy-wait, rough) */
void pit_sleep_ms(u64 ms)
{
    u64 target = pit_ticks + (ms * PIT_TARGET_HZ) / 1000 + 1;
    while (pit_ticks < target)
        __asm__ __volatile__("pause" ::: "memory");
}

void pit_init(void)
{
    u32 divisor = (u32)(PIT_BASE_HZ / PIT_TARGET_HZ);

    /* Mode/Command: channel 0, lobyte/hibyte, mode 2 (rate generator) */
    outb(PIT_COMMAND, PIT_CMD_CHANNEL0 | PIT_CMD_ACCESS_BOTH | PIT_CMD_MODE2);

    /* Write divisor LSB then MSB */
    outb(PIT_CHANNEL0, (u8)(divisor & 0xff));
    outb(PIT_CHANNEL0, (u8)((divisor >> 8) & 0xff));
}
