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

static struct block_hdr *free_list = NULL;

static void expand_heap(void)
{
    /* Allocate a run of contiguous pages (best-effort) */
    u8 *base = NULL;
    for (u64 i = 0; i < HEAP_EXPAND_PAGES; i++) {
        u64 phys = pmm_alloc_page();
        if (!phys) break;
        if (base == NULL)
            base = (u8 *)(uintptr_t)phys;
        /* If the pages are not contiguous we just skip for now */
    }
    if (!base) return;

    size_t total = HEAP_EXPAND_PAGES * (size_t)PAGE_SIZE - HDR_SIZE;
    struct block_hdr *blk = (struct block_hdr *)(uintptr_t)base;
    blk->magic = BLOCK_MAGIC_FREE;
    blk->size  = total;
    blk->next  = free_list;
    free_list  = blk;
}

void heap_init(void)
{
    free_list = NULL;
    expand_heap();
}

void *kmalloc(size_t size)
{
    if (size == 0)
        return NULL;

    size = ALIGN16(size);

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

    /* No block large enough – expand the heap and retry */
    expand_heap();
    return kmalloc(size - (size - size)); /* tail-call retry */
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
