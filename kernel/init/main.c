#include <ck/kernel.h>
#include <ck/mm.h>
#include <ck/sched.h>
#include <ck/vfs.h>
#include <ck/multiboot2.h>
#include <ck/types.h>

/* Defined in kernel/shell/shell.c */
void task_shell(void *arg);

/* ── Kernel info banner ─────────────────────────────────────────────── */
static void print_banner(void)
{
    ck_puts("+-----------------------------------------+\n");
    ck_puts("|       ComputeKERNEL  v1.0.0             |\n");
    ck_puts("|   64-bit x86 kernel - production build  |\n");
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

    t = mb2_find_tag(info, MB2_TAG_NETWORK);
    if (t) {
        if (t->size > sizeof(struct mb2_tag))
            ck_printk("[net] boot network packet available (%u bytes)\n",
                      t->size - (u32)sizeof(struct mb2_tag));
        else
            ck_puts("[net] boot network tag is malformed\n");
    } else {
        ck_puts("[net] no boot network packet provided by bootloader\n");
    }
}

static u32 boot_network_packet_size = 0;
static const u8 *boot_network_packet_data = NULL;

u32 ck_boot_network_packet_size(void)
{
    return boot_network_packet_size;
}

const u8 *ck_boot_network_packet_data(void)
{
    return boot_network_packet_data;
}

int ck_network_available(void)
{
    return boot_network_packet_size > 0;
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
    struct mb2_tag *net_tag = mb2_find_tag(mb2, MB2_TAG_NETWORK);
    if (net_tag && net_tag->size > sizeof(struct mb2_tag)) {
        boot_network_packet_size = net_tag->size - (u32)sizeof(struct mb2_tag);
        boot_network_packet_data = ((const u8 *)net_tag) + sizeof(struct mb2_tag);
    } else {
        boot_network_packet_size = 0;
        boot_network_packet_data = NULL;
    }

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

    ck_puts("\n[boot] all subsystems online - starting shell\n\n");

    /* 8. Run the interactive shell directly (never returns) */
    task_shell(NULL);
}
