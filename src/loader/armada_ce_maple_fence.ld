/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_armada_ce_maple_hook)
PHDRS { bridge PT_LOAD FLAGS(5); hook PT_LOAD FLAGS(7); }
__kui_maple_set_kmode = 0x01dd6374;
__kui_maple_resume = 0x01dd5b10;
SECTIONS {
    . = 0x01dd67a4;
    .bridge : { KEEP(*(.bridge)) } :bridge
    ASSERT(__kui_maple_bridge_end == 0x01dd6800, "Traffic bridge exceeds admitted tail")
    . = 0xacfcd000;
    .hook : {
        KEEP(*(.hook.entry))
        *(.text .text.* .rodata .rodata.*)
        . = ALIGN(4);
        __kui_maple_hook_binary_end = .;
    } :hook
    .workspace (NOLOAD) : {
        . = ALIGN(4);
        __kui_maple_workspace_start = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __kui_maple_workspace_end = .;
    } :hook
    ASSERT(. <= 0xacfcf800, "Traffic code/workspace overlaps private stack")
    . = 0xacfcf800;
    .stack (NOLOAD) : {
        __kui_maple_stack_bottom = .;
        . += 1536;
        __kui_maple_stack_top = .;
    } :hook
    . = 0xacfcfe00;
    .log (NOLOAD) : {
        _kui_armada_ce_maple_hook_log = .;
        . += 120;
        _kui_armada_ce_maple_traffic_freeze = .;
        . += 4;
    } :hook
    . = 0xacfcfe80;
    .fence (NOLOAD) : {
        _kui_armada_ce_maple_fence_log = .;
        . += 120;
    } :hook
    ASSERT(. <= 0xacfd0000, "Traffic observer leaves reserved pages")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
