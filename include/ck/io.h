#ifndef CK_IO_H
#define CK_IO_H

#include <ck/types.h>

/* Write a byte to an I/O port */
static inline void outb(u16 port, u8 val)
{
    __asm__ __volatile__("outb %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/* Read a byte from an I/O port */
static inline u8 inb(u16 port)
{
    u8 ret;
    __asm__ __volatile__("inb %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

/* Write a 16-bit word to an I/O port */
static inline void outw(u16 port, u16 val)
{
    __asm__ __volatile__("outw %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/* Read a 16-bit word from an I/O port */
static inline u16 inw(u16 port)
{
    u16 ret;
    __asm__ __volatile__("inw %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

/* Write a 32-bit dword to an I/O port */
static inline void outl(u16 port, u32 val)
{
    __asm__ __volatile__("outl %0, %1" : : "a"(val), "Nd"(port) : "memory");
}

/* Read a 32-bit dword from an I/O port */
static inline u32 inl(u16 port)
{
    u32 ret;
    __asm__ __volatile__("inl %1, %0" : "=a"(ret) : "Nd"(port) : "memory");
    return ret;
}

/* Short I/O delay (write to unused port 0x80) */
static inline void io_delay(void)
{
    outb(0x80, 0);
}

/* CPU pause hint (for spin-wait loops) */
static inline void cpu_pause(void)
{
    __asm__ __volatile__("pause" ::: "memory");
}

/* Halt the CPU until the next interrupt */
static inline void cpu_hlt(void)
{
    __asm__ __volatile__("hlt" ::: "memory");
}

/* Enable interrupts */
static inline void sti(void)
{
    __asm__ __volatile__("sti" ::: "memory");
}

/* Disable interrupts */
static inline void cli(void)
{
    __asm__ __volatile__("cli" ::: "memory");
}

/* Save and disable interrupts, return old RFLAGS */
static inline u64 irq_save(void)
{
    u64 flags;
    __asm__ __volatile__("pushfq; popq %0; cli" : "=r"(flags) :: "memory");
    return flags;
}

/* Restore interrupt state from saved RFLAGS */
static inline void irq_restore(u64 flags)
{
    __asm__ __volatile__("pushq %0; popfq" :: "rm"(flags) : "memory", "cc");
}

/* Read MSR */
static inline u64 rdmsr(u32 msr)
{
    u32 lo, hi;
    __asm__ __volatile__("rdmsr" : "=a"(lo), "=d"(hi) : "c"(msr));
    return ((u64)hi << 32) | lo;
}

/* Write MSR */
static inline void wrmsr(u32 msr, u64 val)
{
    u32 lo = (u32)val;
    u32 hi = (u32)(val >> 32);
    __asm__ __volatile__("wrmsr" :: "c"(msr), "a"(lo), "d"(hi));
}

/* Read CR2 (faulting address) */
static inline u64 read_cr2(void)
{
    u64 val;
    __asm__ __volatile__("movq %%cr2, %0" : "=r"(val));
    return val;
}

/* Read CR3 (page table base) */
static inline u64 read_cr3(void)
{
    u64 val;
    __asm__ __volatile__("movq %%cr3, %0" : "=r"(val));
    return val;
}

/* Invalidate TLB entry */
static inline void invlpg(u64 vaddr)
{
    __asm__ __volatile__("invlpg (%0)" :: "r"(vaddr) : "memory");
}

#endif /* CK_IO_H */
