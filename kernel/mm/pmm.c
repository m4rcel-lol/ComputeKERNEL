/*
 * kernel/mm/pmm.c
 *
 * Bitmap-based physical page frame allocator.
 *
 * The kernel occupies physical addresses 1 MiB – &_kernel_end.
 * We parse the multiboot2 memory map and mark all usable pages outside
 * the reserved/kernel regions as free.
 *
 * Each bit in the bitmap represents one 4 KiB page.
 * bit = 0 → free, bit = 1 → used.
 *
 * Maximum supported RAM: 4 GiB (identity-mapped by the boot page tables).
 */
#include <ck/mm.h>
#include <ck/multiboot2.h>
#include <ck/kernel.h>
#include <ck/string.h>
#include <ck/types.h>

#define MAX_PHYS_PAGES  (1ULL << 20)   /* 4 GiB / 4 KiB = 1 M pages */
#define BITMAP_WORDS    (MAX_PHYS_PAGES / 64)

/* Kernel symbol exported by the linker script */
extern char _kernel_end[];

static u64  bitmap[BITMAP_WORDS];      /* 0 = free, 1 = used */
static u64  total_pages = 0;
static u64  free_page_count = 0;

/* ── Bitmap helpers ─────────────────────────────────────────────────── */
static inline void bm_set(u64 page)
{
    if (page < MAX_PHYS_PAGES)
        bitmap[page / 64] |= (1ULL << (page % 64));
}

static inline void bm_clear(u64 page)
{
    if (page < MAX_PHYS_PAGES)
        bitmap[page / 64] &= ~(1ULL << (page % 64));
}

static inline int bm_test(u64 page)
{
    if (page >= MAX_PHYS_PAGES)
        return 1;
    return !!(bitmap[page / 64] & (1ULL << (page % 64)));
}

/* ── Bit counting helper (avoids libgcc dependency) ─────────────────── */
static u64 popcount64(u64 x)
{
    x -= (x >> 1) & 0x5555555555555555ULL;
    x = (x & 0x3333333333333333ULL) + ((x >> 2) & 0x3333333333333333ULL);
    x = (x + (x >> 4)) & 0x0f0f0f0f0f0f0f0fULL;
    return (x * 0x0101010101010101ULL) >> 56;
}

void pmm_init(void *mb2_info_ptr)
{
    struct mb2_info        *info  = (struct mb2_info *)mb2_info_ptr;
    struct mb2_tag         *tag;
    struct mb2_tag_mmap    *mmap_tag;
    struct mb2_mmap_entry  *entry;

    /* Start with everything marked used */
    memset(bitmap, 0xff, sizeof(bitmap));
    total_pages     = 0;
    free_page_count = 0;

    /* Find the memory map tag */
    tag = mb2_find_tag(info, MB2_TAG_MMAP);
    if (!tag) {
        ck_puts("[pmm] WARNING: no multiboot2 memory map; PMM unavailable\n");
        return;
    }

    mmap_tag = (struct mb2_tag_mmap *)tag;
    u32 num_entries = (mmap_tag->size - 16) / mmap_tag->entry_size;

    for (u32 i = 0; i < num_entries; i++) {
        entry = (struct mb2_mmap_entry *)
                ((u8 *)mmap_tag->entries + i * mmap_tag->entry_size);

        if (entry->type != MB2_MMAP_AVAILABLE)
            continue;

        u64 base = ALIGN_UP(entry->base_addr, PAGE_SIZE);
        u64 end  = ALIGN_DOWN(entry->base_addr + entry->length, PAGE_SIZE);
        if (end <= base)
            continue;

        for (u64 phys = base; phys < end; phys += PAGE_SIZE) {
            u64 page = phys >> PAGE_SHIFT;
            if (bm_test(page)) {
                bm_clear(page);
                total_pages++;
                free_page_count++;
            }
        }
    }

    /* Re-mark the first 1 MiB (reserved, BIOS, VGA, etc.) */
    for (u64 phys = 0; phys < 0x100000ULL; phys += PAGE_SIZE)
        bm_set(phys >> PAGE_SHIFT);

    /* Re-mark the kernel image pages */
    u64 kend = ALIGN_UP((u64)(uintptr_t)_kernel_end, PAGE_SIZE);
    for (u64 phys = 0x100000ULL; phys < kend; phys += PAGE_SIZE)
        bm_set(phys >> PAGE_SHIFT);

    /* Re-mark the multiboot2 info struct itself */
    u64 mb2_start = ALIGN_DOWN((u64)(uintptr_t)mb2_info_ptr, PAGE_SIZE);
    u64 mb2_end   = ALIGN_UP((u64)(uintptr_t)mb2_info_ptr + info->total_size, PAGE_SIZE);
    for (u64 phys = mb2_start; phys < mb2_end; phys += PAGE_SIZE)
        bm_set(phys >> PAGE_SHIFT);

    /* Recount free pages (bitmap was updated after counting) */
    free_page_count = 0;
    for (u64 w = 0; w < BITMAP_WORDS; w++)
        free_page_count += popcount64(~bitmap[w]);

    ck_printk("[pmm] %llu MiB usable (%llu pages)\n",
              (free_page_count * PAGE_SIZE) >> 20, free_page_count);
}

u64 pmm_alloc_page(void)
{
    for (u64 w = 0; w < BITMAP_WORDS; w++) {
        if (bitmap[w] == ~0ULL)
            continue;
        /* Find the first 0 bit */
        int bit = __builtin_ctzll(~bitmap[w]);
        u64 page = w * 64 + (u64)bit;
        bm_set(page);
        free_page_count--;
        return page << PAGE_SHIFT;
    }
    return 0; /* out of memory */
}

void pmm_free_page(u64 phys)
{
    u64 page = phys >> PAGE_SHIFT;
    if (!bm_test(page)) {
        ck_puts("[pmm] WARNING: double-free detected!\n");
        return;
    }
    bm_clear(page);
    free_page_count++;
}

u64 pmm_total_pages(void) { return total_pages; }
u64 pmm_free_pages(void)  { return free_page_count; }

/* Alias matching the kernel.h public API */
void mm_early_init(void *mb2_info) { pmm_init(mb2_info); }
