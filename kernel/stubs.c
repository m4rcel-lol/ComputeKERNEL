#include <ck/kernel.h>

void mm_early_init(void *boot_info) {
    (void)boot_info;
}

void arch_init(void) {
}

void sched_init(void) {
}

void vfs_init(void) {
}

void init_spawn_first_user(const char *path) {
    (void)path;
}

void arch_halt(void) {
    /* Looping hlt keeps the CPU halted even after interrupt wakeups. */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}
