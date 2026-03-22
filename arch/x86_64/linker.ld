ENTRY(_start)

SECTIONS
{
    /* Link at 0xffffffff80000000 (higher-half x86_64) */
    . = 0xffffffff80000000;

    /* Limine requests must be within the kernel binary */
    .limine_requests : {
        KEEP(*(.limine_requests))
    }

    .text : {
        *(.text*)
    }

    .rodata : ALIGN(4096) {
        *(.rodata*)
    }

    .data : ALIGN(4096) {
        *(.data*)
    }

    .bss : ALIGN(4096) {
        *(COMMON)
        *(.bss*)
    }

    /* Symbol marking the end of the kernel image (used by PMM) */
    _kernel_end = .;
}
