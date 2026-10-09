/* SPDX-License-Identifier: GPL-3.0-only */
/* Isolated Toy control: fixed physical low lease, cached code and uncached
 * mutable state. The ordinary reader's linker scripts are unchanged. */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { resident PT_LOAD FLAGS(5); private PT_LOAD FLAGS(6); }
SECTIONS
{
    . = 0x8c004000;
    .entry : { KEEP(*(.entry)) } :resident
    .text : { *(.text .text.*) } :resident
    .rodata : { *(.rodata .rodata.*) } :resident
    . = ALIGN(32);
    .data (. + 0x20000000) : AT(ADDR(.data) - 0x20000000) {
        __retail_resident_data_begin = .;
        *(.data .data.*)
        __retail_resident_data_end = .;
    } :private
    . = ALIGN(32);
    __retail_resident_binary_end = . - 0x20000000;
    .bss (NOLOAD) : AT(ADDR(.bss) - 0x20000000) {
        __retail_resident_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(32);
        __retail_resident_bss_end = .;
    } :private
    __retail_hook_stack_bottom = 0xac007800;
    __retail_hook_stack = 0xac007d00;
    __retail_hook_stack_bottom_physical = 0x8c007800;
    __retail_hook_stack_physical = 0x8c007d00;
    ASSERT(_start == 0x8c004000, "Toy resident entry moved")
    ASSERT(__retail_resident_bss_end <= __retail_hook_stack_bottom,
           "Toy resident overlaps its guarded service stack")
    ASSERT((__retail_resident_data_begin & 31) == 0,
           "Toy mutable data must begin on a separate cache line")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
