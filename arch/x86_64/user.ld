/* Aurora v2 - fixed-address ring-3 image. */
OUTPUT_FORMAT(elf64-x86-64)
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

PHDRS
{
    abi PT_NOTE FLAGS(4);
    text PT_LOAD FLAGS(5);   /* read + execute */
    rodata PT_LOAD FLAGS(4); /* read only */
    data PT_LOAD FLAGS(6);   /* read + write */
}

SECTIONS
{
    . = 0x400000;

    .text : {
        *(.text.start)
        *(.text .text.*)
    } :text

    . = ALIGN(4096);
    .note.aurora : { KEEP(*(.note.aurora)) } :rodata :abi
    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(4096);
    .data : {
        *(.data .data.*)
    } :data

    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
        *(.got .got.*)
    }
}
