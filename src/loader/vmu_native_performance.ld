/* SPDX-License-Identifier: GPL-3.0-only */
/* Immutable instructions/constants use P1;
 * exports, persistent mutable data and DMA state retain P2. The owned stack
 * is reserved/exported in P2 for cold arm; runtime ephemeral frames use P1. */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_vmu_native_passive_arm)
__kui_native_worker_span = (DEFINED(__kui_native_vmu_lite_worker_layout) ||
    DEFINED(__kui_native_background_compact_worker_layout)) ? 0x12000 :
    DEFINED(__kui_native_general_lean_worker_layout) ? 0x1c000 : 0x20000;
__kui_native_worker_stack_limit = 0xac100000 + __kui_native_worker_span - 0x1000;
PHDRS {
    exports PT_LOAD FLAGS(4);
    code PT_LOAD FLAGS(5);
    state PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0xac100000;
    .vmu_native_worker_exports : AT(0x8c100000) {
        KEEP(*(.vmu_native_worker_exports))
    } :exports
    ASSERT(SIZEOF(.vmu_native_worker_exports)==80,"Native persistent exports must be80bytes")
    . = 0x8c100050;
    .text : AT(ADDR(.text)) { *(.text .text.*) } :code
    .rodata : AT(ADDR(.rodata)) { *(.rodata .rodata.*) } :code
    .immutable_align : AT(ADDR(.immutable_align)) {
        BYTE(0)
        . = ALIGN(32);
    } :code
    . += 0x20000000;
    .data : AT(ADDR(.data) - 0x20000000) { *(.data .data.*) } :state
    .image_align : AT(ADDR(.image_align) - 0x20000000) {
        BYTE(0)
        . = ALIGN(32);
    } :state
    .bss (NOLOAD) : AT(ADDR(.bss) - 0x20000000) {
        __vmu_native_worker_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(32);
        __vmu_native_worker_bss_end = .;
    } :state
    ASSERT(.<=__kui_native_worker_stack_limit,"Native persistent state overlaps its owned4KiB stack")
    . = __kui_native_worker_stack_limit;
    .stack (NOLOAD) : AT(ADDR(.stack) - 0x20000000) {
        __vmu_native_worker_stack_bottom = .;
        . += 4096;
        __vmu_native_worker_stack_top = .;
    } :state
    __vmu_native_worker_end = .;
    ASSERT(__vmu_native_worker_end==0xac100000+__kui_native_worker_span,
           "Native persistent worker span differs from its selected layout")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
