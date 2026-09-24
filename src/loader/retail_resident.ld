/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { resident PT_LOAD FLAGS(7); }
SECTIONS
{
    . = 0x8c008300;
    .entry : { KEEP(*(.entry)) } :resident
    .text : { *(.text .text.*) } :resident
    .rodata : { *(.rodata .rodata.*) } :resident
    .data : { *(.data .data.*) } :resident
    . = ALIGN(4);
    __retail_resident_binary_end = .;
    .bss (NOLOAD) : {
        __retail_resident_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __retail_resident_bss_end = .;
    } :resident
    __retail_hook_stack_bottom = 0x8c00bb00;
    __retail_hook_stack = 0x8c00c000;
    ASSERT(_start == 0x8c008300, "resident entry moved")
    ASSERT(__retail_resident_bss_end <= 0x8c00bb00,
           "retail resident overlaps its guarded service stack")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
