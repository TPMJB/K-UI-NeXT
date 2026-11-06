/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_smash_ce_return_probe_entry)
PHDRS { image PT_LOAD FLAGS(7); }
SECTIONS {
    . = 0xac008300;
    __smash_ce_return_begin = .;
    .entry : AT(ADDR(.entry)-0x20000000) { KEEP(*(.entry)) } :image
    .text : AT(ADDR(.text)-0x20000000) { *(.text .text.*) } :image
    .rodata : AT(ADDR(.rodata)-0x20000000) { *(.rodata .rodata.*) . = ALIGN(4); } :image
    .data : AT(ADDR(.data)-0x20000000) { *(.data .data.*) . = ALIGN(4); } :image
    __smash_ce_return_binary_end = .;
    .bss (NOLOAD) : AT(ADDR(.bss)-0x20000000) {
        __smash_ce_return_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __smash_ce_return_bss_end = .;
    } :image
    ASSERT(_kui_smash_ce_return_probe_entry==0xac008300,"CE return entry moved")
    ASSERT(__smash_ce_return_bss_end<=0xac00b800,"CE return state reaches stack")
    .stack 0xac00b800 (NOLOAD) : AT(ADDR(.stack)-0x20000000) {
        __smash_ce_return_stack_bottom = .;
        . += 2048;
        __smash_ce_return_stack_top = .;
    } :image
    ASSERT(__smash_ce_return_stack_top==0xac00c000,"CE return stack moved")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
