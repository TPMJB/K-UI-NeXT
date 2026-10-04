/* SPDX-License-Identifier: GPL-3.0-only */
/* The Windows CE boot test's reader (KUI_RETAIL_CE): KUI_RETAIL_CE_LIMIT and
 * KUI_RETAIL_CE_HOOK_STACK in retail_loader_layout.h. */
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
    __retail_hook_stack_bottom = 0x8c00c000;
    __retail_hook_stack = 0x8c00c800;
    ASSERT(_start == 0x8c008300, "resident entry moved")
    ASSERT(_kui_retail_ce_kernel == 0x8c008324, "CE kernel slot moved")
    ASSERT(__retail_resident_bss_end <= 0x8c00c000,
           "retail resident overlaps its guarded service stack")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
