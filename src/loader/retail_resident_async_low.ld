/* SPDX-License-Identifier: GPL-3.0-only */
/* Opt-in native firmware-area placement; same image and stack budgets. */
/* The background SCI reader's resident: as retail_resident.ld, with the
 * 352-byte guarded stack its call-graph check allows (retail_loader_layout.h). */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { resident PT_LOAD FLAGS(7); }
SECTIONS
{
    . = 0x8c004000;
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
    __retail_hook_stack_bottom = 0x8c007ba0;
    __retail_hook_stack = 0x8c007d00;
    ASSERT(_start == 0x8c004000, "resident entry moved")
    ASSERT(__retail_resident_bss_end <= 0x8c007ba0,
           "retail resident overlaps its guarded service stack")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
