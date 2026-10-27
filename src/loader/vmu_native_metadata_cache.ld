/* SPDX-License-Identifier: GPL-3.0-only */
/* Explicit metadata-cache isolation layout. Same physical 128KiB lease. Immutable instructions/constants use P1;
 * exports, persistent mutable data and DMA state retain P2. The owned stack
 * is reserved/exported in P2 for cold arm; runtime ephemeral frames use P1. */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_vmu_native_passive_arm)
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
        /* Each metadata object has exclusive complete cache lines. Symbols
         * retain their canonical P2 addresses and original C object sizes. */
        . = ALIGN(32);
        __kui_native_metadata_begin = .;
        __kui_native_metadata_config_begin = .;
        *(.bss.native_metadata.config)
        . = ALIGN(32);
        __kui_native_metadata_config_end = .;
        __kui_native_metadata_live_begin = .;
        *(.bss.native_metadata.live)
        . = ALIGN(32);
        __kui_native_metadata_live_end = .;
        __kui_native_metadata_lifetime_begin = .;
        *(.bss.native_metadata.lifetime)
        . = ALIGN(32);
        __kui_native_metadata_lifetime_end = .;
        __kui_native_metadata_hooks_begin = .;
        *(.bss.native_metadata.hooks)
        . = ALIGN(32);
        __kui_native_metadata_hooks_end = .;
        __kui_native_metadata_end = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(32);
        __vmu_native_worker_bss_end = .;
    } :state
    ASSERT(.<=0xac11f000,"Native persistent state overlaps its owned4KiB stack")
    . = 0xac11f000;
    .stack (NOLOAD) : AT(ADDR(.stack) - 0x20000000) {
        __vmu_native_worker_stack_bottom = .;
        . += 4096;
        __vmu_native_worker_stack_top = .;
    } :state
    __vmu_native_worker_end = .;
    ASSERT(__vmu_native_worker_end==0xac120000,"Native persistent worker span must be128KiB")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
