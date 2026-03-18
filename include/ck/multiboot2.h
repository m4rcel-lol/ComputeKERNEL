/*
 * Minimal multiboot2 tag definitions needed by the kernel.
 * See: https://www.gnu.org/software/grub/manual/multiboot2/multiboot.html
 */
#ifndef CK_MULTIBOOT2_H
#define CK_MULTIBOOT2_H

#include <ck/types.h>

#define MULTIBOOT2_MAGIC 0xe85250d6U
#define MULTIBOOT2_INFO_MAGIC 0x36d76289U

/* Tag types */
#define MB2_TAG_END           0
#define MB2_TAG_CMDLINE       1
#define MB2_TAG_BOOT_LOADER   2
#define MB2_TAG_MODULE        3
#define MB2_TAG_BASIC_MEMINFO 4
#define MB2_TAG_BOOTDEV       5
#define MB2_TAG_MMAP          6
#define MB2_TAG_VBE           7
#define MB2_TAG_FRAMEBUFFER   8
#define MB2_TAG_ELF_SECTIONS  9
#define MB2_TAG_APM           10
#define MB2_TAG_EFI32         11
#define MB2_TAG_EFI64         12
#define MB2_TAG_SMBIOS        13
#define MB2_TAG_ACPI_OLD      14
#define MB2_TAG_ACPI_NEW      15
#define MB2_TAG_NETWORK       16
#define MB2_TAG_EFI_MMAP      17
#define MB2_TAG_LOAD_BASE     21

/* Memory map entry types */
#define MB2_MMAP_AVAILABLE   1
#define MB2_MMAP_RESERVED    2
#define MB2_MMAP_ACPI_RECLM  3
#define MB2_MMAP_NVS         4
#define MB2_MMAP_BADRAM      5

/* Root info block (starts at mb2_info pointer) */
struct mb2_info {
    u32 total_size;
    u32 reserved;
    /* tags follow immediately after */
} __attribute__((packed));

/* Generic tag header */
struct mb2_tag {
    u32 type;
    u32 size;
} __attribute__((packed));

/* Tag: basic memory info */
struct mb2_tag_basic_meminfo {
    u32 type;
    u32 size;
    u32 mem_lower; /* KB below 1 MiB */
    u32 mem_upper; /* KB above 1 MiB */
} __attribute__((packed));

/* Tag: memory map */
struct mb2_mmap_entry {
    u64 base_addr;
    u64 length;
    u32 type;
    u32 reserved;
} __attribute__((packed));

struct mb2_tag_mmap {
    u32 type;
    u32 size;
    u32 entry_size;
    u32 entry_version;
    struct mb2_mmap_entry entries[];
} __attribute__((packed));

/* Tag: command line */
struct mb2_tag_cmdline {
    u32 type;
    u32 size;
    char string[];
} __attribute__((packed));

/* Tag: boot loader name */
struct mb2_tag_bootloader {
    u32 type;
    u32 size;
    char string[];
} __attribute__((packed));

/* Iterate over MB2 tags */
#define MB2_TAG_NEXT(t) \
    ((struct mb2_tag *)(((u8 *)(t)) + ALIGN_UP((t)->size, 8)))

/* Find the first tag of a given type; returns NULL if not found */
static inline struct mb2_tag *mb2_find_tag(struct mb2_info *info, u32 type)
{
    struct mb2_tag *tag = (struct mb2_tag *)((u8 *)info + 8);
    while (tag->type != MB2_TAG_END) {
        if (tag->type == type)
            return tag;
        tag = MB2_TAG_NEXT(tag);
    }
    return NULL;
}

#endif /* CK_MULTIBOOT2_H */
