#ifndef CK_ARCH_GDT_H
#define CK_ARCH_GDT_H

#include <ck/types.h>

/* GDT segment selectors */
#define GDT_NULL        0x00
#define GDT_KCODE       0x08
#define GDT_KDATA       0x10
#define GDT_UCODE32     0x18
#define GDT_UDATA       0x20
#define GDT_UCODE64     0x28
#define GDT_TSS_LOW     0x30   /* TSS occupies two slots (128-bit) */

/* RPL / TI bits for selector arithmetic */
#define SEG_RPL(sel, rpl) ((sel) | (rpl))
#define USER_RPL 3

/* 64-bit GDT entry */
struct gdt_entry {
    u16 limit_low;
    u16 base_low;
    u8  base_mid;
    u8  access;
    u8  granularity;
    u8  base_high;
} __attribute__((packed));

/* 128-bit TSS descriptor (two consecutive GDT entries) */
struct gdt_tss_entry {
    struct gdt_entry low;
    u32 base_high32;
    u32 reserved;
} __attribute__((packed));

/* GDT pointer for lgdt */
struct gdt_ptr {
    u16 limit;
    u64 base;
} __attribute__((packed));

/* Hardware Task State Segment (64-bit) */
struct tss64 {
    u32 reserved0;
    u64 rsp0;        /* ring-0 stack pointer */
    u64 rsp1;
    u64 rsp2;
    u64 reserved1;
    u64 ist[7];      /* interrupt stack table */
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
} __attribute__((packed));

void gdt_init(void);
void gdt_set_kernel_stack(u64 rsp0);

#endif /* CK_ARCH_GDT_H */
