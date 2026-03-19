#include <ck/kernel.h>
#include <ck/mm.h>
#include <ck/sched.h>
#include <ck/vfs.h>
#include <ck/multiboot2.h>
#include <ck/types.h>
#include <ck/io.h>

/* Defined in kernel/arch/x86_64/pit.c */
u64 pit_get_ticks(void);
void pit_sleep_ms(u64 ms);

/* Defined in kernel/arch/x86_64/keyboard.c */
int keyboard_getchar(void);

/* ── Kernel info banner ─────────────────────────────────────────────── */
static void print_banner(void)
{
    ck_puts("+-----------------------------------------+\n");
    ck_puts("|       ComputeKERNEL  v1.0.0             |\n");
    ck_puts("|   64-bit x86 kernel — production build  |\n");
    ck_puts("+-----------------------------------------+\n\n");
}

/* ── Print multiboot2 memory map info ───────────────────────────────── */
static void print_meminfo(struct mb2_info *info)
{
    struct mb2_tag *t;

    t = mb2_find_tag(info, MB2_TAG_BASIC_MEMINFO);
    if (t) {
        struct mb2_tag_basic_meminfo *bm = (struct mb2_tag_basic_meminfo *)t;
        ck_printk("[mem] lower: %u KiB  upper: %u KiB (%u MiB)\n",
                  bm->mem_lower, bm->mem_upper, bm->mem_upper >> 10);
    }

    t = mb2_find_tag(info, MB2_TAG_MMAP);
    if (t) {
        struct mb2_tag_mmap *mmap = (struct mb2_tag_mmap *)t;
        u32 n = (mmap->size - 16) / mmap->entry_size;
        ck_printk("[mem] memory map (%u entries):\n", n);
        for (u32 i = 0; i < n; i++) {
            struct mb2_mmap_entry *e = (struct mb2_mmap_entry *)
                ((u8 *)mmap->entries + i * mmap->entry_size);
            const char *type_str;
            switch (e->type) {
            case MB2_MMAP_AVAILABLE: type_str = "Available"; break;
            case MB2_MMAP_RESERVED:  type_str = "Reserved";  break;
            case MB2_MMAP_ACPI_RECLM: type_str = "ACPI";     break;
            case MB2_MMAP_NVS:       type_str = "NVS";       break;
            case MB2_MMAP_BADRAM:    type_str = "BadRAM";    break;
            default:                 type_str = "Unknown";   break;
            }
            ck_printk("  [%016llx - %016llx]  %s\n",
                      e->base_addr,
                      e->base_addr + e->length - 1,
                      type_str);
        }
    }

    t = mb2_find_tag(info, MB2_TAG_CMDLINE);
    if (t) {
        struct mb2_tag_cmdline *cmd = (struct mb2_tag_cmdline *)t;
        ck_printk("[boot] cmdline: %s\n", cmd->string);
    }

    t = mb2_find_tag(info, MB2_TAG_BOOT_LOADER);
    if (t) {
        struct mb2_tag_bootloader *bl = (struct mb2_tag_bootloader *)t;
        ck_printk("[boot] loader: %s\n", bl->string);
    }
}

/* ── Kernel demo tasks ──────────────────────────────────────────────── */

/* Task A: reads /etc/motd and prints it */
static void task_motd(void *arg)
{
    (void)arg;
    pit_sleep_ms(100);

    int fd = vfs_open("/etc/motd", O_RDONLY);
    if (fd >= 0) {
        char buf[256];
        ssize_t n = vfs_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            ck_printk("[motd] %s", buf);
        }
        vfs_close(fd);
    }
}

/* Task B: simple counter that prints every second */
static void task_heartbeat(void *arg)
{
    (void)arg;
    u64 count = 0;
    for (;;) {
        pit_sleep_ms(1000);
        ck_printk("[heartbeat] tick %llu  (free pages: %llu)\n",
                  count++, pmm_free_pages());
        if (count >= 10)
            break;
    }
    ck_puts("[heartbeat] done.\n");
}

/* Task C: filesystem demo – creates a file, writes to it, reads it back */
static void task_fs_demo(void *arg)
{
    (void)arg;
    pit_sleep_ms(200);

    /* Create a file */
    int fd = vfs_open("/tmp/hello.txt", O_CREAT | O_RDWR);
    if (fd < 0) {
        ck_puts("[fs_demo] could not create /tmp/hello.txt\n");
        return;
    }
    const char *msg = "Hello from ComputeKERNEL ramfs!\n";
    vfs_write(fd, msg, strlen(msg));
    vfs_close(fd);

    /* Read it back */
    fd = vfs_open("/tmp/hello.txt", O_RDONLY);
    if (fd >= 0) {
        char buf[64];
        ssize_t n = vfs_read(fd, buf, sizeof(buf) - 1);
        if (n > 0) {
            buf[n] = '\0';
            ck_printk("[fs_demo] read back: %s", buf);
        }
        vfs_close(fd);
    }

    /* List /etc directory */
    fd = vfs_open("/etc", O_RDONLY);
    if (fd >= 0) {
        char name[VFS_NAME_MAX];
        ck_puts("[fs_demo] /etc contents:\n");
        for (u32 i = 0; vfs_readdir(fd, i, name) == 0; i++)
            ck_printk("  %s\n", name);
        vfs_close(fd);
    }
}

/* ── Kernel main ────────────────────────────────────────────────────── */
void kmain(unsigned int mb2_magic, unsigned int mb2_info_phys)
{
    /* 1. Early console (VGA + serial) */
    ck_early_console_init();
    serial_init();

    print_banner();
    serial_puts("ComputeKERNEL: boot sequence starting\n");

    /* 2. Validate multiboot2 magic */
    if (mb2_magic != MULTIBOOT2_INFO_MAGIC) {
        ck_printk("[boot] ERROR: invalid multiboot2 magic %08x\n", mb2_magic);
        arch_halt();
    }

    struct mb2_info *mb2 = (struct mb2_info *)(uintptr_t)mb2_info_phys;

    /* 3. Architecture init (GDT, IDT, PIC, PIT – enables interrupts) */
    arch_init();

    /* 4. Physical memory manager (parses multiboot2 map) */
    ck_puts("[mm] initialising physical memory manager ...\n");
    mm_early_init(mb2);

    /* Print memory layout */
    print_meminfo(mb2);

    /* 5. Kernel heap */
    ck_puts("[mm] initialising kernel heap ...\n");
    heap_init();
    ck_printk("[mm] heap ready (%llu pages free)\n", pmm_free_pages());

    /* 6. Virtual filesystem */
    ck_puts("[vfs] initialising virtual filesystem ...\n");
    vfs_init();

    /* 7. Scheduler */
    ck_puts("[sched] initialising scheduler ...\n");
    sched_init();

    /* Create kernel tasks */
    u64 irq_flags = irq_save();
    task_create("motd",      task_motd,      NULL, 0);
    task_create("heartbeat", task_heartbeat, NULL, 0);
    task_create("fs_demo",   task_fs_demo,   NULL, 0);
    irq_restore(irq_flags);

    ck_puts("\n[boot] all subsystems online — handing off to scheduler\n\n");

    /* 8. Yield to the scheduler (never returns) */
    sched_start();

    /* Should never reach here */
    arch_halt();
}
