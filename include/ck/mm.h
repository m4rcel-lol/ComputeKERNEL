#ifndef CK_MM_H
#define CK_MM_H

#include <ck/types.h>

/* ── Physical Memory Manager ────────────────────────────────────────── */

void  pmm_init(void *mb2_info);       /* call once at boot */
u64   pmm_alloc_page(void);           /* returns physical address or 0 */
u64   pmm_alloc_pages(u64 n);         /* allocate n contiguous pages */
void  pmm_free_page(u64 phys);
void  pmm_free_pages_range(u64 phys, u64 n);
u64   pmm_total_pages(void);
u64   pmm_free_pages(void);

/* ── Kernel Virtual Heap ────────────────────────────────────────────── */

void  heap_init(void);
void *kmalloc(size_t size);
void *kzalloc(size_t size);           /* allocate + zero */
void  kfree(void *ptr);

/* Convenience: allocate exactly n pages of physically-contiguous RAM */
void *alloc_pages(size_t n);          /* returns virtual == physical addr */
void  free_pages(void *ptr, size_t n);

/* Resize a heap allocation (NULL-safe, preserves data on failure) */
void *krealloc(void *ptr, size_t size);

#endif /* CK_MM_H */
