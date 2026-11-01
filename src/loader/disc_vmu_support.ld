/* SPDX-License-Identifier: GPL-3.0-only */
/* Relocatable support lives in the allocator lease immediately after the
 * 72KiB worker. Keep real-GD firmware RAM untouched. No private extra stack. */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_disc_vmu_support_bind)
PHDRS {
    exports PT_LOAD FLAGS(4);
    code PT_LOAD FLAGS(5);
    state PT_LOAD FLAGS(6);
}
SECTIONS {
    . = 0xac112000;
    .disc_vmu_support_exports : AT(0x8c112000) {
        KEEP(*(.disc_vmu_support_exports))
    } :exports
    ASSERT(SIZEOF(.disc_vmu_support_exports)==64,"Disc VMU support exports must be64bytes")
    . = 0x8c112040;
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
        __disc_vmu_support_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(32);
        __disc_vmu_support_bss_end = .;
    } :state
    ASSERT(.<=0xac113fe0,"Disc VMU support exceeds owned8KiB tail")
    .reservation 0xac113fe0 (NOLOAD) : AT(0x8c113fe0) {
        . += 32;
    } :state
    __disc_vmu_support_end = .;
    ASSERT(__disc_vmu_support_end==0xac114000,"Disc VMU support reservation differs")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
