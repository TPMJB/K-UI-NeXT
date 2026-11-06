/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_vc2_ce_maple_bridge)
SECTIONS {
    . = 0x01ce7c34;
    .vc2_maple_bridge : { KEEP(*(.vc2_maple_bridge)) }
    ASSERT(__kui_vc2_maple_bridge_end == 0x01ce7c98,
        "VC2 worker bridge must occupy only its admitted 100-byte slice")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
