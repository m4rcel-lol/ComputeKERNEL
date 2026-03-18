#include <ck/kernel.h>

void kmain(void *boot_info) {
    ck_early_console_init();
    ck_printk("ComputeKERNEL: boot start\n");
    mm_early_init(boot_info);
    arch_init();
    sched_init();
    vfs_init();
    init_spawn_first_user("/sbin/init");
    /* Should never return. */
    arch_halt();
}
