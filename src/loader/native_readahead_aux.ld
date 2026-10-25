/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_native_readahead_aux_visit)
PHDRS {
    exports PT_LOAD FLAGS(4);
    context PT_LOAD FLAGS(6);
    code PT_LOAD FLAGS(7);
}
SECTIONS {
    . = 0x8c421000;
    .exports : { KEEP(*(.native_ra_exports)) } :exports
    ASSERT(SIZEOF(.exports)==64,"Read-ahead exports must be64bytes")
    . = 0xac421040;
    .context : AT(ADDR(.context)-0x20000000) {
        KEEP(*(.data.native_ra_context))
        *(.data .data.*)
    } :context
    .bss (NOLOAD) : AT(ADDR(.bss)-0x20000000) {
        *(.bss .bss.* COMMON)
    } :context
    ASSERT(.<=0xac422000,"Read-ahead context exceeds its owned page")
    . = 0x8c422000;
    .text : { *(.text .text.*) } :code
    .rodata : { *(.rodata .rodata.*) } :code
    .image_align : { BYTE(0) . = ALIGN(32); } :code
    __native_ra_image_end = .;
    ASSERT(.<=0x8c426000,"Read-ahead code exceeds its owned16KiB budget")
    . = 0x8c427000;
    .stack (NOLOAD) : {
        __native_ra_stack_bottom = .+0x20000000;
        . += 0x1000;
        __native_ra_stack_top = .+0x20000000;
    } :code
    __native_ra_memory_end = .+0x20000000;
    ASSERT(__native_ra_memory_end==0xac428000,"Read-ahead auxiliary span changed")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
