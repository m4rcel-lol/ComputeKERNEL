#include <ck/kernel.h>

void kmain(void) {
    ck_early_console_init();
    ck_printk("ComputeKERNEL: boot start\n");
    mm_early_init(0);
    arch_init();
    sched_init();
    vfs_init();
    init_spawn_first_user("/sbin/init");
    for (;;) {
        arch_halt();
    }
}

