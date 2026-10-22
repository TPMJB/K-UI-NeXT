/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(7); }
SECTIONS
{
    . = 0x8ce00000;
    .entry : { KEEP(*(.entry)) } :image
    .text : { *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    .data : { *(.data .data.*) } :image
    .client_image : { KEEP(*(.client_image)) } :image
    . = ALIGN(4);
    __binary_end = .;
    ASSERT(__binary_end <= 0x8cf00000, "resident binary exceeds 1 MiB")
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } :image
    ASSERT(__bss_end <= 0x8cfc0000, "resident data overlaps GD hook stack guard")
    ASSERT(SIZEOF(.client_image) <= 0x100000, "client binary exceeds relocation bounds")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
