/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_vc2_ce_vmu_prepare)
/* The initializer and cache-array leaves execute through P2; ordinary
 * immutable C executes through P1. All initialized bytes have contiguous
 * main-RAM LMAs, including the mapped bridge's source copy. The bridge's
 * VMA remains its eventual CE location so relative calls are resolved once. */
PHDRS { uncached PT_LOAD FLAGS(5); cached PT_LOAD FLAGS(5);
        bridge PT_LOAD FLAGS(5); private PT_LOAD FLAGS(6); }
SECTIONS {
    . = 0xacfc4000;
    .uncached : AT(0x8cfc4000) {
        KEEP(*(.prepare.entry))
        KEEP(*(.hook.entry))
        *(.text.vc2_prepare_publish)
        *(.text.kui_armada_ce_excerpt_array_read)
        *(.text.kui_armada_ce_cache_snapshot)
        . = ALIGN(32);
        __kui_maple_uncached_end = .;
    } :uncached
    ASSERT(_kui_vc2_ce_vmu_prepare == 0xacfc4000,
        "VC2 initializer must be the source-bound fixed entry")
    . -= 0x20000000;
    .hook : AT(ADDR(.hook)) {
        __kui_maple_cached_start = .;
        *(.text .text.* .rodata .rodata.*)
        . = ALIGN(32);
        __kui_maple_cached_end = .;
    } :cached
    __vc2_maple_bridge_load = LOADADDR(.hook)+SIZEOF(.hook);
    .vc2_maple_bridge 0x01ce7c34 : AT(__vc2_maple_bridge_load) {
        KEEP(*(.vc2_maple_bridge))
    } :bridge
    ASSERT(__kui_vc2_maple_bridge_end == 0x01ce7c98,
        "VC2 worker bridge exceeds its 100-byte source slice")
    __vc2_maple_bridge_blob_start = LOADADDR(.vc2_maple_bridge)+0x20000000;
    __vc2_maple_bridge_blob_end = __vc2_maple_bridge_blob_start+SIZEOF(.vc2_maple_bridge);
    __vc2_maple_bridge_callback_blob = __vc2_maple_bridge_blob_start+
        (__kui_vc2_maple_bridge_helper_literal-ADDR(.vc2_maple_bridge));
    __kui_maple_hook_binary_end = __vc2_maple_bridge_blob_end;
    ASSERT((__kui_maple_hook_binary_end & 3) == 0 &&
           __kui_maple_hook_binary_end < 0xacfcf800,
           "VC2 immutable end must bound an aligned nonempty working tail")
    . = __kui_maple_hook_binary_end;
    .workspace (NOLOAD) : AT(ADDR(.workspace)-0x20000000) {
        . = ALIGN(4);
        __kui_maple_workspace_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __kui_maple_workspace_end = .;
    } :private
    .unexpected_data : { *(.data .data.*) }
    ASSERT(SIZEOF(.unexpected_data) == 0,"VC2 helper has initialized mutable data")
    ASSERT(. <= 0xacfcf800,"VC2 helper code/state overlaps private callback stack")
    . = 0xacfcf800;
    .stack (NOLOAD) : AT(ADDR(.stack)-0x20000000) {
        __kui_maple_stack_bottom = .;
        . += 1536;
        __kui_maple_stack_top = .;
    } :private
    . = 0xacfcfe00;
    .log (NOLOAD) : AT(ADDR(.log)-0x20000000) {
        _kui_armada_ce_maple_hook_log = .; . += 120;
        _kui_armada_ce_maple_traffic_freeze = .; . += 4;
    } :private
    . = 0xacfcfe80;
    .fence (NOLOAD) : AT(ADDR(.fence)-0x20000000) {
        _kui_armada_ce_maple_fence_log = .; . += 120;
    } :private
    . = 0xacfcff00;
    .a2 (NOLOAD) : AT(ADDR(.a2)-0x20000000) {
        _kui_armada_ce_vmu_a2_log = .; . += 120;
        _kui_armada_ce_maple_auto_reason = .; . += 4;
        _kui_armada_ce_maple_auto_phase = .; . += 4;
        _kui_armada_ce_maple_auto_menu_expected = .; . += 4;
        _kui_armada_ce_maple_proof_initial = .; . += 4;
        _kui_armada_ce_maple_proof_final = .; . += 4;
    } :private
    ASSERT(. <= 0xacfd0000,"VC2 helper leaves pre-NK reservation")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
