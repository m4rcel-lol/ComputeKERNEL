#include <ck/kernel.h>

void kmain(unsigned int mb2_magic, unsigned int mb2_info)
{
    (void)mb2_magic;
    (void)mb2_info;

    ck_early_console_init();
    ck_puts("ComputeKERNEL: boot start\n");
    mm_early_init((void *)(unsigned long)mb2_info);
    arch_init();
    sched_init();
    vfs_init();
    ck_puts("ComputeKERNEL: halting.\n");
    arch_halt();
}
