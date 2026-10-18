/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(7); bootstrap PT_LOAD FLAGS(5); }
SECTIONS {
    . = 0xac00e000;
    .bootstrap2 : { KEEP(*(.bootstrap2)) } :bootstrap
    . = 0x8c010000;
    .entry : { KEEP(*(.entry)) } :image
    .text : { *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    .data : { *(.data .data.*) } :image
    . = ALIGN(4);
    __binary_end = .;
    .bss (NOLOAD) : {
        __bss_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __bss_end = .;
    } :image
    ASSERT(__binary_end <= 0x8c040000, "CHD fixture executable too large")
    ASSERT(__bss_end <= 0x8c0f0000, "CHD fixture overlaps its stack")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
