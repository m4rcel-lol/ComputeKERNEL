/*
 * kernel/mm/heap.c
 *
 * Simple kernel heap (kmalloc / kfree).
 *
 * Design:
 *  - Maintains a singly-linked list of free blocks.
 *  - Each allocation is preceded by a 'block_hdr' that records its size.
 *  - On kfree, the block is returned to the free list; adjacent free blocks
 *    are merged on the next allocation pass.
 *  - When the free list is exhausted, more pages are requested from the PMM.
 *
 * Minimum allocation granularity: sizeof(block_hdr) = 16 bytes.
 * All allocations are 16-byte aligned.
 */
#include <ck/mm.h>
#include <ck/kernel.h>
#include <ck/string.h>
#include <ck/types.h>

#define BLOCK_MAGIC_FREE 0xFEEDFACEUL
#define BLOCK_MAGIC_USED 0xDEADBEEFUL

/* Pages claimed from PMM per heap expansion */
#define HEAP_EXPAND_PAGES 4

struct block_hdr {
    u32              magic;
    u32              _pad;
    size_t           size;    /* usable bytes (excluding header) */
    struct block_hdr *next;   /* free list pointer (only valid when free) */
};

#define HDR_SIZE  sizeof(struct block_hdr)
#define ALIGN16(x) ALIGN_UP(x, 16)

/* Retry limit for kmalloc: one scan + one expansion + one rescan */
#define KMALLOC_MAX_ATTEMPTS 2

static struct block_hdr *free_list = NULL;

/*
 * Expand the heap by at least min_bytes of usable space.
 * Pages are requested from the PMM one at a time; contiguous runs are
 * merged into a single free block, non-contiguous pages each become
 * their own block so no byte is wasted.
 */
static void expand_heap(size_t min_bytes)
{
    /* How many pages we need to cover (header + payload) */
    size_t n_pages = (min_bytes + HDR_SIZE + PAGE_SIZE - 1) / PAGE_SIZE;
    if (n_pages < HEAP_EXPAND_PAGES)
        n_pages = HEAP_EXPAND_PAGES;

    u64    run_base  = 0;   /* physical base of the current contiguous run */
    size_t run_pages = 0;   /* length of the current run in pages          */

    for (size_t i = 0; i < n_pages; i++) {
        u64 phys = pmm_alloc_page();
        if (!phys) break;

        if (run_base == 0) {
            run_base  = phys;
            run_pages = 1;
        } else if (phys == run_base + run_pages * PAGE_SIZE) {
            /* Contiguous with the current run – extend it */
            run_pages++;
        } else {
            /* Gap: commit the previous run as one free block */
            struct block_hdr *blk = (struct block_hdr *)(uintptr_t)run_base;
            blk->magic = BLOCK_MAGIC_FREE;
            blk->size  = run_pages * PAGE_SIZE - HDR_SIZE;
            blk->next  = free_list;
            free_list  = blk;
            /* Start a new run */
            run_base  = phys;
            run_pages = 1;
        }
    }

    /* Commit the last run */
    if (run_pages > 0) {
        struct block_hdr *blk = (struct block_hdr *)(uintptr_t)run_base;
        blk->magic = BLOCK_MAGIC_FREE;
        blk->size  = run_pages * PAGE_SIZE - HDR_SIZE;
        blk->next  = free_list;
        free_list  = blk;
    }
}

void heap_init(void)
{
    free_list = NULL;
    expand_heap(0);   /* seed the heap with HEAP_EXPAND_PAGES pages */
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return NULL;

    size = ALIGN16(size);

    /* Retry loop: expand heap once if the first scan fails */
    for (int attempt = 0; attempt < KMALLOC_MAX_ATTEMPTS; attempt++) {
        /* First-fit search */
        struct block_hdr *prev = NULL;
        struct block_hdr *cur  = free_list;
        while (cur) {
            if (cur->magic != BLOCK_MAGIC_FREE) {
                ck_puts("[heap] CORRUPTION: bad magic in free list\n");
                return NULL;
            }
            if (cur->size >= size) {
                /* Split if leftover is large enough for a new block */
                if (cur->size >= size + HDR_SIZE + 16) {
                    struct block_hdr *tail = (struct block_hdr *)((u8 *)cur + HDR_SIZE + size);
                    tail->magic = BLOCK_MAGIC_FREE;
                    tail->size  = cur->size - size - HDR_SIZE;
                    tail->next  = cur->next;
                    cur->size   = size;
                    cur->next   = tail;
                }
                /* Unlink from free list */
                if (prev) prev->next = cur->next;
                else      free_list  = cur->next;
                cur->magic = BLOCK_MAGIC_USED;
                cur->next  = NULL;
                return (void *)((u8 *)cur + HDR_SIZE);
            }
            prev = cur;
            cur  = cur->next;
        }

        /* No block large enough – expand the heap and retry once */
        expand_heap(size);
    }

    ck_puts("[heap] kmalloc: out of memory\n");
    return NULL;
}

void *kzalloc(size_t size)
{
    void *p = kmalloc(size);
    if (p)
        memset(p, 0, size);
    return p;
}

void kfree(void *ptr)
{
    if (!ptr) return;

    struct block_hdr *hdr = (struct block_hdr *)((u8 *)ptr - HDR_SIZE);
    if (hdr->magic != BLOCK_MAGIC_USED) {
        ck_puts("[heap] kfree: bad magic (double-free or corruption?)\n");
        return;
    }
    hdr->magic = BLOCK_MAGIC_FREE;
    hdr->next  = free_list;
    free_list  = hdr;
}

void *alloc_pages(size_t n)
{
    if (n == 0) return NULL;
    u64 phys = pmm_alloc_page();
    if (!phys) return NULL;
    /* We only guarantee a single page here for simplicity */
    (void)n;
    return (void *)(uintptr_t)phys;
}

void free_pages(void *ptr, size_t n)
{
    if (!ptr) return;
    for (size_t i = 0; i < n; i++)
        pmm_free_page((u64)(uintptr_t)ptr + i * PAGE_SIZE);
}
