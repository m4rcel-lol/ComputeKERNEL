#include <ck/kernel.h>
#include <ck/mm.h>
#include <ck/sched.h>
#include <ck/vfs.h>
#include <limine.h>
#include <ck/types.h>

/* Defined in kernel/shell/shell.c */
void task_shell(void *arg);

/* Limine requests */
__attribute__((used, section(".limine_requests")))
LIMINE_REQUESTS_START_MARKER

__attribute__((used, section(".limine_requests")))
LIMINE_BASE_REVISION(0)

__attribute__((used, section(".limine_requests")))
struct limine_framebuffer_request limine_framebuffer_request = {
    .id = LIMINE_FRAMEBUFFER_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
struct limine_memmap_request limine_memmap_request = {
    .id = LIMINE_MEMMAP_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
struct limine_hhdm_request limine_hhdm_request = {
    .id = LIMINE_HHDM_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
struct limine_kernel_address_request limine_kernel_address_request = {
    .id = LIMINE_KERNEL_ADDRESS_REQUEST,
    .revision = 0
};

__attribute__((used, section(".limine_requests")))
LIMINE_REQUESTS_END_MARKER

/* ── Kernel info banner ─────────────────────────────────────────────── */
static void print_banner(void)
{
    ck_puts("+-----------------------------------------+\n");
    ck_puts("|       ComputeKERNEL  v1.1.0 (Limine)    |\n");
    ck_puts("|   64-bit x86 kernel - production build  |\n");
    ck_puts("+-----------------------------------------+\n\n");
}

/* ── Print Limine memory map info ───────────────────────────────────── */
static void print_meminfo(void)
{
    if (limine_memmap_request.response == NULL) {
        ck_puts("[mem] ERROR: no memory map provided by Limine\n");
        return;
    }

    struct limine_memmap_response *mmap = limine_memmap_request.response;
    ck_printk("[mem] memory map (%llu entries):\n", (unsigned long long)mmap->entry_count);

    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *e = mmap->entries[i];
        const char *type_str;
        switch (e->type) {
        case LIMINE_MEMMAP_USABLE:                 type_str = "Usable";     break;
        case LIMINE_MEMMAP_RESERVED:               type_str = "Reserved";   break;
        case LIMINE_MEMMAP_ACPI_RECLAIMABLE:       type_str = "ACPI Recl";  break;
        case LIMINE_MEMMAP_ACPI_NVS:               type_str = "ACPI NVS";   break;
        case LIMINE_MEMMAP_BAD_MEMORY:             type_str = "Bad RAM";    break;
        case LIMINE_MEMMAP_BOOTLOADER_RECLAIMABLE: type_str = "Boot Recl";  break;
        case LIMINE_MEMMAP_KERNEL_AND_MODULES:     type_str = "Kernel/Mod"; break;
        case LIMINE_MEMMAP_FRAMEBUFFER:            type_str = "Framebuffer";break;
        default:                                   type_str = "Unknown";    break;
        }
        ck_printk("  [%016llx - %016llx]  %s\n",
                  (unsigned long long)e->base,
                  (unsigned long long)(e->base + e->length - 1),
                  type_str);
    }
}

/* ── Kernel main ────────────────────────────────────────────────────── */
void kmain(void)
{
    /* 1. Early console (VGA + serial) */
    ck_early_console_init();
    serial_init();

    print_banner();
    serial_puts("ComputeKERNEL: boot sequence starting (Limine)\n");

    /* 2. Validate Limine base revision */
    if (!LIMINE_BASE_REVISION_SUPPORTED) {
        ck_puts("[boot] ERROR: Limine base revision not supported\n");
        arch_halt();
    }

    /* 3. Architecture init (GDT, IDT, PIC, PIT – enables interrupts) */
    arch_init();

    /* 4. Physical memory manager (parses Limine map) */
    ck_puts("[mm] initialising physical memory manager ...\n");
    mm_early_init();

    /* Print memory layout */
    print_meminfo();

    /* 5. Kernel heap */
    ck_puts("[mm] initialising kernel heap ...\n");
    heap_init();
    ck_printk("[mm] heap ready (%llu pages free)\n", (unsigned long long)pmm_free_pages());

    /* 6. Network stack foundation */
    ck_puts("[net] initialising lwIP-oriented TCP/IP stack foundation ...\n");
    net_init();

    /* 7. NIC framework */
    ck_puts("[net] initialising network device driver framework ...\n");
    nic_init();

    /* 8. SSH daemon scaffold */
    ck_puts("[ssh] initialising secure shell daemon scaffold ...\n");
    sshd_init();

    /* 9. Virtual filesystem */
    ck_puts("[vfs] initialising virtual filesystem ...\n");
    vfs_init();

    /* 10. Scheduler */
    ck_puts("[sched] initialising scheduler ...\n");
    sched_init();

    ck_puts("\n[boot] all subsystems online - creating shell task\n\n");

    /* 11. Create shell task and start scheduler */
    if (task_create("cksh", task_shell, NULL, 0) < 0) {
        ck_puts("[sched] failed to create shell task\n");
        arch_halt();
    }

    sched_start();
}
