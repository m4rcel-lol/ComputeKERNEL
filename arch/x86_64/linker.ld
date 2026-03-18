ENTRY(_start)

SECTIONS
{
    . = 1M;

    /* Multiboot2 header must appear within the first 32 KiB of the image */
    .multiboot2 : { *(.multiboot2) }

    .text : {
        *(.text.entry32)   /* 32-bit boot entry + long-mode switch */
        *(.text*)
    }

    .rodata : ALIGN(4096) { *(.rodata*) }
    .data   : ALIGN(4096) { *(.data*)   }

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    }
}

