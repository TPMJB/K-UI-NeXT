/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_toy_pilot_initialize)
/* The opt-in profile keeps code cached but gives every writable object and
 * private stack a P2 VMA. LMAs remain in the original physical reservation,
 * so objcopy and the installer still carry one compact binary. */
__toy_private_offset = DEFINED(KUI_TOY_PRIVATE_P2) ?
    (KUI_TOY_PRIVATE_P2 ? 0x20000000 : 0) : 0;
PHDRS { worker PT_LOAD FLAGS(7); private PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0x8cfd0000;
    .toy_exports : { KEEP(*(.toy_exports)) } :worker
    ASSERT(SIZEOF(.toy_exports)==76, "Toy exports must be 76 bytes")
    .text : { *(.text .text.*) } :worker
    .rodata : { *(.rodata .rodata.*) } :worker
    .data (__toy_private_offset ? ALIGN(32) + __toy_private_offset : .) :
        AT(ADDR(.data) - __toy_private_offset) {
        __toy_pilot_data_begin = .;
        *(.data .data.*)
        __toy_pilot_data_end = .;
    } :private
    /* Include the alignment span in the private memory segment, starting at
     * the compact file end even when .data is empty. This keeps baseline ELF
     * flattening identical to objcopy's payload. */
    .private_align (ADDR(.data) + SIZEOF(.data)) (NOLOAD) :
        AT(ADDR(.private_align) - __toy_private_offset) {
        . = ALIGN(32);
    } :private
    __toy_pilot_binary_end = . - __toy_private_offset;
    .bss (NOLOAD) : AT(ADDR(.bss) - __toy_private_offset) {
        __toy_pilot_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(32);
        __toy_pilot_bss_end = .;
    } :private
    .gd_stack (NOLOAD) : AT(ADDR(.gd_stack) - __toy_private_offset) {
        __toy_pilot_gd_stack_bottom = .;
        . += DEFINED(_kui_toy_pilot_gd_bridge) ? 4096 : 0;
        __toy_pilot_gd_stack_top = .;
    } :private
    .stack (NOLOAD) : AT(ADDR(.stack) - __toy_private_offset) {
        __toy_pilot_stack_bottom = .;
        . += 8192;
        __toy_pilot_stack_top = .;
    } :private
    __toy_pilot_worker_end = . - __toy_private_offset;
    ASSERT(__toy_pilot_worker_end<=0x8cfe0000,
        "Toy worker/stack overlaps fixed SDK high scratch")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
