#ifndef CK_ARCH_IDT_H
#define CK_ARCH_IDT_H

#include <ck/types.h>

/* Interrupt frame pushed by ISR stubs + CPU */
struct interrupt_frame {
    /* Pushed by isr_common (in push order, low address first) */
    u64 r15, r14, r13, r12;
    u64 r11, r10, r9,  r8;
    u64 rbp, rdi, rsi, rdx;
    u64 rcx, rbx, rax;
    /* Pushed by ISR stub */
    u64 vector;
    u64 error_code;
    /* Pushed by CPU on interrupt */
    u64 rip, cs, rflags;
    u64 rsp, ss;            /* only valid on privilege-level change */
} __attribute__((packed));

/* 64-bit IDT gate descriptor */
struct idt_gate {
    u16 offset_low;
    u16 selector;
    u8  ist;        /* interrupt stack table index (bits 2:0); 0 = use RSP */
    u8  flags;      /* P | DPL | type: 0x8E = present, DPL=0, 64-bit interrupt gate */
    u16 offset_mid;
    u32 offset_high;
    u32 reserved;
} __attribute__((packed));

struct idt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

#define IDT_FLAG_PRESENT    0x80
#define IDT_FLAG_DPL0       0x00
#define IDT_FLAG_DPL3       0x60
#define IDT_FLAG_INTR_GATE  0x0E
#define IDT_FLAG_TRAP_GATE  0x0F

/* Exception vectors */
#define EXC_DE   0   /* Divide Error */
#define EXC_DB   1   /* Debug */
#define EXC_NMI  2   /* Non-Maskable Interrupt */
#define EXC_BP   3   /* Breakpoint */
#define EXC_OF   4   /* Overflow */
#define EXC_BR   5   /* Bound Range Exceeded */
#define EXC_UD   6   /* Invalid Opcode */
#define EXC_NM   7   /* Device Not Available */
#define EXC_DF   8   /* Double Fault */
#define EXC_TS   10  /* Invalid TSS */
#define EXC_NP   11  /* Segment Not Present */
#define EXC_SS   12  /* Stack-Segment Fault */
#define EXC_GP   13  /* General Protection Fault */
#define EXC_PF   14  /* Page Fault */
#define EXC_MF   16  /* x87 FPU Exception */
#define EXC_AC   17  /* Alignment Check */
#define EXC_MC   18  /* Machine Check */
#define EXC_XM   19  /* SIMD Floating Point Exception */

/* IRQ base vector (after PIC remapping) */
#define IRQ_BASE 32

void idt_init(void);
void idt_set_handler(u8 vector, void (*handler)(void), u8 ist, u8 flags);

/* C-level interrupt dispatcher (called from isr_common) */
void interrupt_dispatch(struct interrupt_frame *frame);

#endif /* CK_ARCH_IDT_H */
