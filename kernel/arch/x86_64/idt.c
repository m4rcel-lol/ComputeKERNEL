/*
 * kernel/arch/x86_64/idt.c
 *
 * Builds the 256-entry IDT, wires up all ISR stubs, and provides a C-level
 * interrupt dispatcher that handles CPU exceptions with helpful messages.
 */
#include <ck/arch/idt.h>
#include <ck/arch/gdt.h>
#include <ck/kernel.h>
#include <ck/io.h>
#include <ck/types.h>
#include <ck/string.h>

/* Forward declarations for ISR stubs in boot/x86_64/isr_stubs.S */
#define DECLARE_ISR(n) extern void _isr##n(void);
DECLARE_ISR(0)  DECLARE_ISR(1)  DECLARE_ISR(2)  DECLARE_ISR(3)
DECLARE_ISR(4)  DECLARE_ISR(5)  DECLARE_ISR(6)  DECLARE_ISR(7)
DECLARE_ISR(8)  DECLARE_ISR(9)  DECLARE_ISR(10) DECLARE_ISR(11)
DECLARE_ISR(12) DECLARE_ISR(13) DECLARE_ISR(14) DECLARE_ISR(15)
DECLARE_ISR(16) DECLARE_ISR(17) DECLARE_ISR(18) DECLARE_ISR(19)
DECLARE_ISR(20) DECLARE_ISR(21) DECLARE_ISR(22) DECLARE_ISR(23)
DECLARE_ISR(24) DECLARE_ISR(25) DECLARE_ISR(26) DECLARE_ISR(27)
DECLARE_ISR(28) DECLARE_ISR(29) DECLARE_ISR(30) DECLARE_ISR(31)
DECLARE_ISR(32) DECLARE_ISR(33) DECLARE_ISR(34) DECLARE_ISR(35)
DECLARE_ISR(36) DECLARE_ISR(37) DECLARE_ISR(38) DECLARE_ISR(39)
DECLARE_ISR(40) DECLARE_ISR(41) DECLARE_ISR(42) DECLARE_ISR(43)
DECLARE_ISR(44) DECLARE_ISR(45) DECLARE_ISR(46) DECLARE_ISR(47)
DECLARE_ISR(48) DECLARE_ISR(49) DECLARE_ISR(50) DECLARE_ISR(51)
DECLARE_ISR(52) DECLARE_ISR(53) DECLARE_ISR(54) DECLARE_ISR(55)
DECLARE_ISR(56) DECLARE_ISR(57) DECLARE_ISR(58) DECLARE_ISR(59)
DECLARE_ISR(60) DECLARE_ISR(61) DECLARE_ISR(62) DECLARE_ISR(63)
DECLARE_ISR(64) DECLARE_ISR(255)

static struct idt_gate idt[256] __attribute__((aligned(16)));
static struct idt_ptr  idt_pointer;

/* IRQ handlers registered by drivers */
typedef void (*irq_handler_t)(void);
static irq_handler_t irq_handlers[16];

static void idt_set_gate(u8 vec, void (*stub)(void), u8 ist, u8 flags)
{
    u64 offset = (u64)(uintptr_t)stub;
    struct idt_gate *g = &idt[vec];

    g->offset_low  = (u16)(offset & 0xffff);
    g->selector    = GDT_KCODE;
    g->ist         = ist & 0x7;
    g->flags       = flags;
    g->offset_mid  = (u16)((offset >> 16) & 0xffff);
    g->offset_high = (u32)(offset >> 32);
    g->reserved    = 0;
}

void idt_set_handler(u8 vector, void (*handler)(void), u8 ist, u8 flags)
{
    idt_set_gate(vector, handler, ist, flags);
}

void idt_init(void)
{
    memset(idt, 0, sizeof(idt));

#define INSTALL(n) idt_set_gate(n, _isr##n, 0, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INTR_GATE)
    INSTALL(0);  INSTALL(1);  INSTALL(2);  INSTALL(3);
    INSTALL(4);  INSTALL(5);  INSTALL(6);  INSTALL(7);
    /* Double-fault uses IST1 so it always has a clean stack */
    idt_set_gate(8, _isr8, 1, IDT_FLAG_PRESENT | IDT_FLAG_DPL0 | IDT_FLAG_INTR_GATE);
    INSTALL(9);  INSTALL(10); INSTALL(11); INSTALL(12);
    INSTALL(13); INSTALL(14); INSTALL(15); INSTALL(16);
    INSTALL(17); INSTALL(18); INSTALL(19); INSTALL(20);
    INSTALL(21); INSTALL(22); INSTALL(23); INSTALL(24);
    INSTALL(25); INSTALL(26); INSTALL(27); INSTALL(28);
    INSTALL(29); INSTALL(30); INSTALL(31);
    /* IRQs */
    INSTALL(32); INSTALL(33); INSTALL(34); INSTALL(35);
    INSTALL(36); INSTALL(37); INSTALL(38); INSTALL(39);
    INSTALL(40); INSTALL(41); INSTALL(42); INSTALL(43);
    INSTALL(44); INSTALL(45); INSTALL(46); INSTALL(47);
    INSTALL(48); INSTALL(49); INSTALL(50); INSTALL(51);
    INSTALL(52); INSTALL(53); INSTALL(54); INSTALL(55);
    INSTALL(56); INSTALL(57); INSTALL(58); INSTALL(59);
    INSTALL(60); INSTALL(61); INSTALL(62); INSTALL(63);
    INSTALL(64); INSTALL(255);
#undef INSTALL

    idt_pointer.limit = (u16)(sizeof(idt) - 1);
    idt_pointer.base  = (u64)(uintptr_t)idt;

    __asm__ __volatile__("lidt (%0)" :: "r"(&idt_pointer) : "memory");
}

/* Register a handler for hardware IRQ n (0-15) */
void irq_register(int irq, irq_handler_t handler)
{
    if (irq >= 0 && irq < 16)
        irq_handlers[irq] = handler;
}

/* Exception names */
static const char *exc_names[] = {
    "Divide Error",           /* 0 */
    "Debug",                  /* 1 */
    "Non-Maskable Interrupt", /* 2 */
    "Breakpoint",             /* 3 */
    "Overflow",               /* 4 */
    "Bound Range Exceeded",   /* 5 */
    "Invalid Opcode",         /* 6 */
    "Device Not Available",   /* 7 */
    "Double Fault",           /* 8 */
    "Coprocessor Overrun",    /* 9 */
    "Invalid TSS",            /* 10 */
    "Segment Not Present",    /* 11 */
    "Stack-Segment Fault",    /* 12 */
    "General Protection",     /* 13 */
    "Page Fault",             /* 14 */
    "Reserved",               /* 15 */
    "x87 FP Exception",       /* 16 */
    "Alignment Check",        /* 17 */
    "Machine Check",          /* 18 */
    "SIMD FP Exception",      /* 19 */
    "Virtualization",         /* 20 */
    "Control Protection",     /* 21 */
};

/* C-level interrupt dispatcher – called from _isr_common */
void interrupt_dispatch(struct interrupt_frame *frame)
{
    u64 vec = frame->vector;

    if (vec < IRQ_BASE) {
        /* CPU exception */
        const char *name = (vec < ARRAY_SIZE(exc_names)) ? exc_names[vec] : "Unknown Exception";

        ck_printk("\n*** EXCEPTION %llu: %s ***\n", vec, name);
        ck_printk("  RIP=%016llx  CS=%04llx  RFLAGS=%016llx\n",
                  frame->rip, frame->cs, frame->rflags);
        ck_printk("  RSP=%016llx  SS=%04llx\n",
                  frame->rsp, frame->ss);
        ck_printk("  RAX=%016llx  RBX=%016llx  RCX=%016llx\n",
                  frame->rax, frame->rbx, frame->rcx);
        ck_printk("  RDX=%016llx  RSI=%016llx  RDI=%016llx\n",
                  frame->rdx, frame->rsi, frame->rdi);
        ck_printk("  Error code: %016llx\n", frame->error_code);

        if (vec == 14) {
            /* Page fault: print CR2 (faulting address) */
            ck_printk("  CR2 (fault addr): %016llx\n", read_cr2());
            ck_printk("  PF flags: %s %s %s\n",
                      (frame->error_code & 1) ? "PROT" : "NOT-PRESENT",
                      (frame->error_code & 2) ? "WRITE" : "READ",
                      (frame->error_code & 4) ? "USER" : "KERNEL");
        }

        if (vec == 8) {
            /* Double fault is unrecoverable */
            ck_puts("  [DOUBLE FAULT – system halted]\n");
            for (;;) __asm__ __volatile__("cli; hlt");
        }

        /* For now, halt on all unhandled exceptions */
        ck_puts("  [System halted]\n");
        for (;;) __asm__ __volatile__("cli; hlt");

    } else if (vec < IRQ_BASE + 16) {
        /* Hardware IRQ */
        int irq = (int)(vec - IRQ_BASE);

        if (irq_handlers[irq])
            irq_handlers[irq]();

        /* Send End-Of-Interrupt to PIC(s) */
        if (irq >= 8)
            outb(0xa0, 0x20); /* slave PIC EOI */
        outb(0x20, 0x20);     /* master PIC EOI */
    }
    /* Spurious / unknown vectors: silently ignore */
}
