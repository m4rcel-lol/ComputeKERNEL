/* ComputeKERNEL linker script for x86_64 */

OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5);   /* R-X */
    rodata PT_LOAD FLAGS(4);   /* R-- */
    data   PT_LOAD FLAGS(6);   /* RW- */
}

SECTIONS
{
    /* Kernel is linked at -2 GiB (kernel code model virtual address). */
    . = 0xFFFFFFFF80000000;

    .text : AT(ADDR(.text) - 0xFFFFFFFF80000000)
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
    } :text

    . = ALIGN(4096);

    .rodata :
    {
        *(.rodata .rodata.*)
    } :rodata

    /* Read-only data: exception frames, Rust metadata */
    .eh_frame_hdr :
    {
        *(.eh_frame_hdr)
    } :rodata

    .eh_frame :
    {
        KEEP(*(.eh_frame))
    } :rodata

    . = ALIGN(4096);

    .data :
    {
        *(.data .data.*)
    } :data

    .bss :
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    } :data

    . = ALIGN(4096);

    /* Discard debug sections to keep the image small. */
    /DISCARD/ :
    {
        *(.comment)
        *(.note.*)
        *(.debug*)
    }
}
