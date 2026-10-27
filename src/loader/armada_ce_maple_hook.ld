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
    ASSERT(__kui_maple_bridge_end == 0x01dd6800, "Maple bridge must fit the exact 92-byte admitted tail")
    . = 0xacfcf000;
    .hook : { KEEP(*(.hook)) } :hook
    ASSERT(__kui_maple_hook_binary_end <= 0xacfcfe00, "Maple hook overlaps retained log")
    . = 0xacfcfe00;
    .log (NOLOAD) : { KEEP(*(.log)) } :hook
    ASSERT(. == 0xacfcfe78, "Maple hook log moved")
    ASSERT(. <= 0xacfd0000, "Maple hook leaves reserved page")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
