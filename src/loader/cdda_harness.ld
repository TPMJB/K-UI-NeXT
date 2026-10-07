/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); stack PT_LOAD FLAGS(6); }
SECTIONS
{
    . = 0x8c010000;
    .entry : { KEEP(*(.entry)) } :image
    .text : { *(.text .text.*) } :image
    .rodata : { *(.rodata .rodata.*) } :image
    . = ALIGN(4);
    .data : { KEEP(*(.cdda_boot_marker)) *(.data .data.* .sdata .sdata.*) } :data
    . = ALIGN(32);
    __cdda_bss_begin = .;
    .bss (NOLOAD) : { *(.bss .bss.* .sbss .sbss.*) *(COMMON) . = ALIGN(32); } :data
    __cdda_bss_end = .;
    ASSERT(__cdda_bss_end <= 0x8c200000, "CDDA image overlaps private stack")
    . = 0x8c200000;
    .stack (NOLOAD) : {
        __cdda_stack_bottom = .;
        . += 0x10000;
        __cdda_stack_top = .;
    } :stack
    ASSERT(__cdda_stack_top == 0x8c210000, "CDDA private stack layout changed")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
