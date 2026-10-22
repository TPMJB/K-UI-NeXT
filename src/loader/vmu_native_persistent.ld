/* SPDX-License-Identifier: GPL-3.0-only */
/* Native durable worker uses 128KiB of the unchanged 192KiB heap lease. */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_vmu_native_passive_arm)
PHDRS { worker PT_LOAD FLAGS(7); }
SECTIONS {
    . = 0xac100000;
    .vmu_native_worker_exports : AT(ADDR(.vmu_native_worker_exports) - 0x20000000) {
        KEEP(*(.vmu_native_worker_exports))
    } :worker
    ASSERT(SIZEOF(.vmu_native_worker_exports)==80,"Native persistent exports must be80bytes")
    .text : AT(ADDR(.text) - 0x20000000) { *(.text .text.*) } :worker
    .rodata : AT(ADDR(.rodata) - 0x20000000) { *(.rodata .rodata.*) } :worker
    .data : AT(ADDR(.data) - 0x20000000) { *(.data .data.*) } :worker
    .image_align : AT(ADDR(.image_align) - 0x20000000) {
        BYTE(0)
        . = ALIGN(32);
    } :worker
    .bss (NOLOAD) : AT(ADDR(.bss) - 0x20000000) {
        __vmu_native_worker_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(32);
        __vmu_native_worker_bss_end = .;
    } :worker
    ASSERT(.<=0xac11f000,"Native persistent state overlaps its owned4KiB stack")
    . = 0xac11f000;
    .stack (NOLOAD) : AT(ADDR(.stack) - 0x20000000) {
        __vmu_native_worker_stack_bottom = .;
        . += 4096;
        __vmu_native_worker_stack_top = .;
    } :worker
    __vmu_native_worker_end = .;
    ASSERT(__vmu_native_worker_end==0xac120000,"Native persistent worker span must be128KiB")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
