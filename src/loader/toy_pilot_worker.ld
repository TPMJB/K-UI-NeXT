/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_toy_pilot_initialize)
PHDRS { worker PT_LOAD FLAGS(7); }
SECTIONS {
    . = 0x8cfd0000;
    .toy_exports : { KEEP(*(.toy_exports)) } :worker
    ASSERT(SIZEOF(.toy_exports)==64, "Toy exports must be 64 bytes")
    .text : { *(.text .text.*) } :worker
    .rodata : { *(.rodata .rodata.*) } :worker
    .data : { *(.data .data.*) } :worker
    . = ALIGN(32);
    __toy_pilot_binary_end = .;
    .bss (NOLOAD) : {
        __toy_pilot_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(32);
        __toy_pilot_bss_end = .;
    } :worker
    .stack (NOLOAD) : {
        __toy_pilot_stack_bottom = .;
        . += 8192;
        __toy_pilot_stack_top = .;
    } :worker
    __toy_pilot_worker_end = .;
    ASSERT(__toy_pilot_worker_end<=0x8cfe0000,
        "Toy worker/stack overlaps fixed SDK high scratch")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
