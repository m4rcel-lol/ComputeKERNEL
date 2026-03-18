/*
 * kernel/stubs.c – intentionally empty.
 *
 * All subsystems that were formerly stubbed out here now have real
 * implementations:
 *   mm_early_init  → kernel/mm/pmm.c
 *   arch_init      → kernel/arch/x86_64/cpu.c
 *   arch_halt      → kernel/arch/x86_64/cpu.c
 *   sched_init     → kernel/sched/sched.c
 *   vfs_init       → kernel/vfs/vfs.c
 *   heap_init      → kernel/mm/heap.c
 *   kmalloc/kfree  → kernel/mm/heap.c
 */
