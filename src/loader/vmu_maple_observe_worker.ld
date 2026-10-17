/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_vmu_maple_observer_init)
PHDRS { worker PT_LOAD FLAGS(5); private PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x8cfd0000;
    .vmu_observe_exports : { KEEP(*(.vmu_observe_exports)) } :worker
    ASSERT(SIZEOF(.vmu_observe_exports)==56,"Maple observer exports size")
    .text : { *(.text .text.*) } :worker
    .rodata : { *(.rodata .rodata.*) } :worker
    . = ALIGN(32);
    /* Advance dot independently: an empty .data output section can be
     * discarded by GNU ld, but the following BSS/stack must remain P2. */
    . += 0x20000000;
    .data : AT(ADDR(.data) - 0x20000000) {
        *(.data .data.*)
    } :private
    .private_align (NOLOAD) : AT(ADDR(.private_align) - 0x20000000) {
        . = ALIGN(32);
    } :private
    __vmu_observe_binary_end = . - 0x20000000;
    .bss (NOLOAD) : AT(ADDR(.bss) - 0x20000000) {
        __vmu_observe_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(32);
        __vmu_observe_bss_end = .;
    } :private
    .stack (NOLOAD) : AT(ADDR(.stack) - 0x20000000) {
        __vmu_observe_stack_bottom = .;
        . += 4096;
        __vmu_observe_stack_top = .;
    } :private
    __vmu_observe_worker_end = . - 0x20000000;
    ASSERT(__vmu_observe_worker_end<=0x8cfe0000,
        "Maple observer overlaps fixed SDK high scratch")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
