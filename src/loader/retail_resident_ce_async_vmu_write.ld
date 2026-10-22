/* SPDX-License-Identifier: GPL-3.0-only */
/* Explicit writable CE VMU profile only. The ordinary readonly CE linker
 * remains unchanged. Runtime code/state fits below dc00 after excluding the
 * periodic framebuffer meter; the guarded 1 KiB service stack ends at the
 * original bootstrap2 boundary e000. Nothing reserves bootstrap/VBR RAM. */
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
    __retail_hook_stack_bottom = 0x8c00dc00;
    __retail_hook_stack = 0x8c00e000;
    ASSERT(_start == 0x8c008300, "resident entry moved")
    ASSERT(_kui_retail_ce_kernel == 0x8c008324, "CE kernel slot moved")
    ASSERT(DEFINED(__kui_maple_write_runtime_resident), "Writable resident requires explicit source runtime marker")
    ASSERT(__kui_maple_write_runtime_resident == 1, "Writable resident source profile changed")
    ASSERT(__retail_resident_binary_end <= 0x8c00d800, "Writable resident code leaves service pointer bounds")
    ASSERT(__retail_resident_bss_end <= 0x8c00dc00,
           "writable retail resident overlaps its guarded service stack")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
