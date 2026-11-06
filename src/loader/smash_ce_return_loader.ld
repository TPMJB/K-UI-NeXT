/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_smash_ce_return_entry)
PHDRS { image PT_LOAD FLAGS(7); }
SECTIONS {
    __kui_smash_ce_return_startup_abi = ABSOLUTE(1);
    __smash_ce_return_copy_pinned = DEFINED(__kui_smash_swirl_private_return) ? 0xac009a40 : 0xac0099a0;
    __smash_ce_return_runtime_pinned = DEFINED(__kui_smash_swirl_private_return) ? 0xac009dd4 : 0xac009d34;
    __smash_ce_return_end_pinned = DEFINED(__kui_smash_swirl_private_return) ? 0xac00a04c : 0xac009fac;
    . = 0xac008300;
    __smash_ce_return_begin = .;
    .entry : AT(ADDR(.entry)-0x20000000) { KEEP(*(.entry)) } :image
    .text : AT(ADDR(.text)-0x20000000) {
        /* Keep exit-only additions out of the accepted pre-copy span. */
        *(EXCLUDE_FILE (*cold_load_crc32.o *ce-controller-ram-clear.o */src/loader/smash_ce_return_loader.o) .text)
        *(EXCLUDE_FILE (*cold_load_crc32.o *ce-controller-ram-clear.o */src/loader/smash_ce_return_loader.o) .text.*)
        */src/loader/smash_ce_return_loader.o(.text.runtime_failures .text.stopped .text.kui_smash_ce_return_main)
    } :image
    ASSERT(.<=__smash_ce_return_copy_pinned,"CE return pre-copy code exceeds accepted startup span")
    .copy __smash_ce_return_copy_pinned : AT(ADDR(.copy)-0x20000000) { KEEP(*(.ce_return_copy)) } :image
    .exit_text : AT(ADDR(.exit_text)-0x20000000) {
        */src/loader/smash_ce_return_loader.o(.text.restore_raw)
        *ce-controller-ram-clear.o(.text .text.*)
        *cold_load_crc32.o(.text .text.*)
    } :image
    .rodata : AT(ADDR(.rodata)-0x20000000) { *(.rodata .rodata.*) . = ALIGN(4); } :image
    ASSERT(.<=__smash_ce_return_runtime_pinned,"CE return exit code/data exceeds accepted startup span")
    .data __smash_ce_return_runtime_pinned : AT(ADDR(.data)-0x20000000) { *(.data .data.*) . = ALIGN(4); } :image
    __smash_ce_return_binary_end = .;
    ASSERT(__smash_ce_return_binary_end-__smash_ce_return_begin <= 7968,
        "CE return initialized transport exceeds the original helper padding")
    ASSERT(__smash_ce_return_copy==__smash_ce_return_copy_pinned,
        "CE return startup copy entry moved")
    ASSERT(_kui_smash_ce_return_runtime==__smash_ce_return_runtime_pinned,
        "CE return startup runtime moved")
    ASSERT(__smash_ce_return_binary_end==__smash_ce_return_end_pinned,
        "CE return initialized startup extent moved")
    .bss (NOLOAD) : AT(ADDR(.bss)-0x20000000) {
        __smash_ce_return_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __smash_ce_return_bss_end = .;
    } :image
    ASSERT(__smash_ce_return_bss_end-__smash_ce_return_bss_begin == 16,
        "CE return copy leaf must zero every one of the four private BSS words")
    ASSERT(_kui_smash_ce_return_previous_e0==__smash_ce_return_end_pinned,
        "CE return previous firmware forwarding cell moved")
    ASSERT(_kui_smash_ce_return_entry==0xac008300,"CE return entry moved")
    ASSERT(__smash_ce_return_bss_end<=0xac00b800,"CE return state reaches private stack")
    .stack 0xac00b800 (NOLOAD) : AT(ADDR(.stack)-0x20000000) {
        __smash_ce_return_stack_bottom = .;
        . += 2048;
        __smash_ce_return_stack_top = .;
    } :image
    ASSERT(__smash_ce_return_stack_top==0xac00c000,"CE return stack moved")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
