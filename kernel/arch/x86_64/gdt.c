/*
 * kernel/arch/x86_64/gdt.c
 *
 * Sets up a proper 64-bit GDT with:
 *   - Null descriptor
 *   - Kernel code  (0x08)
 *   - Kernel data  (0x10)
 *   - User code 32 (0x18)  – placeholder, needed for SYSRET compatibility
 *   - User data    (0x20)
 *   - User code 64 (0x28)
 *   - TSS          (0x30, 128-bit, two entries)
 *
 * Also loads the new GDT and reloads all segment registers.
 */
#include <ck/arch/gdt.h>
#include <ck/types.h>
#include <ck/string.h>

/* Number of 64-bit GDT slots: 6 code/data + 2 for TSS = 8 */
#define GDT_ENTRIES 8

static struct gdt_entry gdt[GDT_ENTRIES] __attribute__((aligned(16)));
static struct tss64     kernel_tss        __attribute__((aligned(16)));
static struct gdt_ptr   gdt_pointer       __attribute__((aligned(16)));

/* Interrupt stack (IST1) used for double-fault and NMI handlers */
static char ist1_stack[4096] __attribute__((aligned(16)));

/* ── Helper: build a 64-bit code/data descriptor ─────────────────── */
static struct gdt_entry make_entry(u32 base, u32 limit,
                                   u8 access, u8 granularity)
{
    struct gdt_entry e;
    e.limit_low   = (u16)(limit & 0xffff);
    e.base_low    = (u16)(base  & 0xffff);
    e.base_mid    = (u8)((base  >> 16) & 0xff);
    e.access      = access;
    e.granularity = (u8)((granularity & 0xf0) | ((limit >> 16) & 0x0f));
    e.base_high   = (u8)((base  >> 24) & 0xff);
    return e;
}

/* ── Helper: install a 128-bit TSS descriptor ─────────────────────── */
static void install_tss(struct gdt_tss_entry *slot, struct tss64 *tss)
{
    u64  base  = (u64)(uintptr_t)tss;
    u32  limit = (u32)sizeof(*tss) - 1;

    slot->low.limit_low   = (u16)(limit & 0xffff);
    slot->low.base_low    = (u16)(base  & 0xffff);
    slot->low.base_mid    = (u8)((base  >> 16) & 0xff);
    slot->low.access      = 0x89;       /* P=1, DPL=0, Type=9 (64-bit TSS available) */
    slot->low.granularity = (u8)((limit >> 16) & 0x0f);
    slot->low.base_high   = (u8)((base  >> 24) & 0xff);
    slot->base_high32     = (u32)(base  >> 32);
    slot->reserved        = 0;
}

/* ── Public: load GDT and initialise TSS ─────────────────────────── */
void gdt_init(void)
{
    /*
     * access byte:
     *   0x9a = P=1 S=1 DPL=0 Type=0xA (code, execute/read)
     *   0x92 = P=1 S=1 DPL=0 Type=0x2 (data, read/write)
     *   0xfa = P=1 S=1 DPL=3 Type=0xA (user code, execute/read)
     *   0xf2 = P=1 S=1 DPL=3 Type=0x2 (user data, read/write)
     * granularity byte for 64-bit:
     *   0x20 = L=1, D/B=0 (64-bit code)
     *   0x00 = L=0, D/B=0 (data; base/limit ignored in 64-bit)
     */
    gdt[0] = make_entry(0, 0,      0x00, 0x00); /* null */
    gdt[1] = make_entry(0, 0xffff, 0x9a, 0x20); /* kernel code 64 */
    gdt[2] = make_entry(0, 0xffff, 0x92, 0x00); /* kernel data */
    gdt[3] = make_entry(0, 0xffff, 0xfa, 0x40); /* user code 32 (compatibility) */
    gdt[4] = make_entry(0, 0xffff, 0xf2, 0x00); /* user data */
    gdt[5] = make_entry(0, 0xffff, 0xfa, 0x20); /* user code 64 */

    /* TSS descriptor occupies gdt[6] and gdt[7] (128-bit) */
    memset(&kernel_tss, 0, sizeof(kernel_tss));
    kernel_tss.iomap_base = (u16)sizeof(kernel_tss);
    /* IST1 is used for double-fault and NMI to guarantee a clean stack */
    kernel_tss.ist[0] = (u64)(uintptr_t)(ist1_stack + sizeof(ist1_stack));

    install_tss((struct gdt_tss_entry *)&gdt[6], &kernel_tss);

    gdt_pointer.limit = (u16)(sizeof(gdt) - 1);
    gdt_pointer.base  = (u64)(uintptr_t)gdt;

    /* Load the new GDT */
    __asm__ __volatile__ (
        "lgdt (%0)\n\t"
        /* Far-return to reload CS with the kernel code selector 0x08 */
        "pushq $0x08\n\t"
        "lea  1f(%%rip), %%rax\n\t"
        "pushq %%rax\n\t"
        "lretq\n\t"
        "1:\n\t"
        /* Reload all data segment registers */
        "movw $0x10, %%ax\n\t"
        "movw %%ax,  %%ds\n\t"
        "movw %%ax,  %%es\n\t"
        "movw %%ax,  %%ss\n\t"
        "xorw %%ax,  %%ax\n\t"
        "movw %%ax,  %%fs\n\t"
        "movw %%ax,  %%gs\n\t"
        :: "r"(&gdt_pointer) : "rax", "memory"
    );

    /* Load TSS (selector 0x30, RPL=0) */
    __asm__ __volatile__("ltr %0" :: "rm"((u16)GDT_TSS_LOW));
}

/* Update RSP0 (kernel stack for ring-0 entry from ring-3) */
void gdt_set_kernel_stack(u64 rsp0)
{
    kernel_tss.rsp0 = rsp0;
}
