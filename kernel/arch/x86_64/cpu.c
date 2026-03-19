/*
 * kernel/arch/x86_64/cpu.c
 *
 * Top-level architecture initialisation.
 * Called from kmain() after the console and serial port are ready.
 */
#include <ck/arch/gdt.h>
#include <ck/arch/idt.h>
#include <ck/kernel.h>
#include <ck/io.h>
#include <ck/types.h>
#include <ck/mouse.h>

/* Defined in pic.c / pit.c / keyboard.c */
void pic_init(void);
void pic_mask_all(void);
void pic_unmask_irq(int irq);
void pit_init(void);
void pit_tick(void);
void keyboard_init(void);
void keyboard_irq_handler(void);
void mouse_irq_handler(void);
void sched_tick(void);
void irq_register(int irq, void (*handler)(void));

/* ── IRQ handlers ───────────────────────────────────────────────────── */
static void irq0_handler(void) /* PIT timer */
{
    pit_tick();
    sched_tick();
}

static void irq1_handler(void) /* PS/2 keyboard */
{
    keyboard_irq_handler();
}

static void irq12_handler(void) /* PS/2 mouse */
{
    mouse_irq_handler();
}

/* ── arch_init ──────────────────────────────────────────────────────── */
void arch_init(void)
{
    ck_puts("[arch] initialising GDT ... ");
    gdt_init();
    ck_puts("OK\n");

    ck_puts("[arch] initialising IDT ... ");
    idt_init();
    ck_puts("OK\n");

    ck_puts("[arch] initialising PIC ... ");
    pic_init();
    pic_mask_all();          /* mask everything first */
    ck_puts("OK\n");

    ck_puts("[arch] initialising PIT (100 Hz) ... ");
    pit_init();
    ck_puts("OK\n");

    ck_puts("[arch] initialising keyboard ... ");
    keyboard_init();
    ck_puts("OK\n");

    ck_puts("[arch] initialising mouse ... ");
    mouse_init();
    if (mouse_is_available())
        ck_puts("OK\n");
    else
        ck_puts("disabled\n");

    /* Register IRQ handlers */
    irq_register(0, irq0_handler);  /* timer */
    irq_register(1, irq1_handler);  /* keyboard */
    irq_register(12, irq12_handler);/* mouse */

    /* Unmask timer and keyboard */
    pic_unmask_irq(0);
    pic_unmask_irq(1);
    pic_unmask_irq(2); /* cascade (required for IRQs 8-15) */
    if (mouse_is_available())
        pic_unmask_irq(12);

    ck_puts("[arch] enabling interrupts\n");
    sti();
}

/* ── arch_halt ──────────────────────────────────────────────────────── */
void arch_halt(void)
{
    ck_puts("[arch] system halted.\n");
    for (;;)
        __asm__ __volatile__("cli; hlt");
}
