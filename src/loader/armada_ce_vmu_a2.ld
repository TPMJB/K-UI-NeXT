/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_armada_ce_maple_hook)
/* One contiguous physical image, with explicit execution/data aliases. The
 * bridge and P2 shim retain their original ABI; only immutable C/rodata use
 * P1. The cache-array windows and every byte they can read remain P2. */
PHDRS { bridge PT_LOAD FLAGS(5); uncached PT_LOAD FLAGS(5);
        cached PT_LOAD FLAGS(5); private PT_LOAD FLAGS(6); }
__kui_maple_set_kmode = 0x01dd6374;
__kui_maple_resume = 0x01dd5b10;
SECTIONS {
    . = 0x01dd67a4;
    .bridge : { KEEP(*(.bridge)) } :bridge
    ASSERT(__kui_maple_bridge_end == 0x01dd6800, "Traffic bridge exceeds admitted tail")
    . = 0xacfca000;
    .uncached : AT(0x8cfca000) {
        KEEP(*(.hook.entry))
        *(.text.kui_armada_ce_excerpt_array_read)
        *(.text.kui_armada_ce_cache_snapshot)
        . = ALIGN(32);
        __kui_maple_uncached_end = .;
    } :uncached
    . -= 0x20000000;
    .hook : AT(ADDR(.hook)) {
        __kui_maple_cached_start = .;
        *(.text .text.* .rodata .rodata.*)
        . = ALIGN(32);
        __kui_maple_cached_end = .;
    } :cached
    __kui_maple_hook_binary_end = . + 0x20000000;
    . += 0x20000000;
    .workspace (NOLOAD) : AT(ADDR(.workspace) - 0x20000000) {
        . = ALIGN(4);
        __kui_maple_workspace_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __kui_maple_workspace_end = .;
    } :private
    .unexpected_data : { *(.data .data.*) }
    ASSERT(SIZEOF(.unexpected_data) == 0, "Maple helper has initialized mutable data")
    ASSERT(. <= 0xacfcf800, "Traffic code/workspace overlaps private stack")
    . = 0xacfcf800;
    .stack (NOLOAD) : AT(ADDR(.stack) - 0x20000000) {
        __kui_maple_stack_bottom = .;
        . += 1536;
        __kui_maple_stack_top = .;
    } :private
    . = 0xacfcfe00;
    .log (NOLOAD) : AT(ADDR(.log) - 0x20000000) {
        _kui_armada_ce_maple_hook_log = .;
        . += 120;
        _kui_armada_ce_maple_traffic_freeze = .;
        . += 4;
    } :private
    . = 0xacfcfe80;
    .fence (NOLOAD) : AT(ADDR(.fence) - 0x20000000) {
        _kui_armada_ce_maple_fence_log = .;
        . += 120;
    } :private
    . = 0xacfcff00;
    .a2 (NOLOAD) : AT(ADDR(.a2) - 0x20000000) {
        _kui_armada_ce_vmu_a2_log = .;
        . += 120;
        _kui_armada_ce_maple_auto_reason = .; . += 4;
        _kui_armada_ce_maple_auto_phase = .; . += 4;
        _kui_armada_ce_maple_auto_menu_expected = .; . += 4;
        _kui_armada_ce_maple_proof_initial = .; . += 4;
        _kui_armada_ce_maple_proof_final = .; . += 4;
    } :private
    ASSERT(. <= 0xacfd0000, "Traffic observer leaves reserved pages")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
