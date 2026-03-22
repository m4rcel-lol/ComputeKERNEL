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
#include <limine.h>
#include <ck/kernel.h>
#include <ck/string.h>
#include <ck/types.h>

#define MAX_PHYS_PAGES  (1ULL << 20)   /* 4 GiB / 4 KiB = 1 M pages */
#define BITMAP_WORDS    (MAX_PHYS_PAGES / 64)

/* Kernel symbol exported by the linker script */
extern char _kernel_end[];

/* Limine request defined in kernel/init/main.c */
extern struct limine_memmap_request limine_memmap_request;
extern struct limine_kernel_address_request limine_kernel_address_request;

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

void pmm_init(void)
{
    struct limine_memmap_response *mmap = limine_memmap_request.response;

    /* Start with everything marked used */
    memset(bitmap, 0xff, sizeof(bitmap));
    total_pages     = 0;
    free_page_count = 0;

    if (!mmap) {
        ck_puts("[pmm] WARNING: no Limine memory map; PMM unavailable\n");
        return;
    }

    for (uint64_t i = 0; i < mmap->entry_count; i++) {
        struct limine_memmap_entry *entry = mmap->entries[i];

        if (entry->type != LIMINE_MEMMAP_USABLE)
            continue;

        u64 base = ALIGN_UP(entry->base, PAGE_SIZE);
        u64 end  = ALIGN_DOWN(entry->base + entry->length, PAGE_SIZE);
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

    /* Re-mark the kernel image pages using Limine's reported physical address */
    if (limine_kernel_address_request.response) {
        struct limine_kernel_address_response *ka = limine_kernel_address_request.response;
        u64 k_phys_base = ka->physical_base;
        u64 k_virt_base = ka->virtual_base;
        u64 k_size = ALIGN_UP((u64)(uintptr_t)_kernel_end, PAGE_SIZE) - k_virt_base;
        
        for (u64 off = 0; off < k_size; off += PAGE_SIZE) {
            bm_set((k_phys_base + off) >> PAGE_SHIFT);
        }
    } else {
        /* Fallback if no kernel address request (shouldn't happen with Limine) */
        u64 kend = ALIGN_UP((u64)(uintptr_t)_kernel_end, PAGE_SIZE);
        for (u64 phys = 0x100000ULL; phys < kend; phys += PAGE_SIZE)
            bm_set(phys >> PAGE_SHIFT);
    }

    /* Recount free pages (bitmap was updated after counting) */
    free_page_count = 0;
    for (u64 w = 0; w < BITMAP_WORDS; w++)
        free_page_count += popcount64(~bitmap[w]);

    ck_printk("[pmm] %llu MiB usable (%llu pages)\n",
              (unsigned long long)((free_page_count * PAGE_SIZE) >> 20), (unsigned long long)free_page_count);
}

u64 pmm_alloc_page(void)
{
    for (u64 w = 0; w < BITMAP_WORDS; w++) {
        if (bitmap[w] == ~0ULL)
            continue;
        /* Find the first 0 bit using ctz (count trailing zeros) */
        int bit = __builtin_ctzll(~bitmap[w]);
        u64 page = w * 64 + (u64)bit;
        bm_set(page);
        free_page_count--;
        return page << PAGE_SHIFT;
    }
    return 0; /* out of memory */
}

/*
 * Allocate n physically contiguous pages.
 * Returns the physical base address, or 0 on failure.
 * Simple linear scan – suitable for small n (DMA buffers, etc.).
 */
u64 pmm_alloc_pages(u64 n)
{
    if (n == 0) return 0;
    if (n == 1) return pmm_alloc_page();

    u64 run_start = 0;
    u64 run_len   = 0;

    for (u64 page = 0; page < MAX_PHYS_PAGES; page++) {
        if (!bm_test(page)) {
            if (run_len == 0) run_start = page;
            run_len++;
            if (run_len == n) {
                /* Found a run – mark all pages used */
                for (u64 i = run_start; i < run_start + n; i++) {
                    bm_set(i);
                    free_page_count--;
                }
                return run_start << PAGE_SHIFT;
            }
        } else {
            run_len = 0;
        }
    }
    return 0; /* no contiguous run found */
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

void pmm_free_pages_range(u64 phys, u64 n)
{
    for (u64 i = 0; i < n; i++)
        pmm_free_page(phys + i * PAGE_SIZE);
}

u64 pmm_total_pages(void) { return total_pages; }
u64 pmm_free_pages(void)  { return free_page_count; }

/* Alias matching the kernel.h public API */
void mm_early_init(void) { pmm_init(); }
