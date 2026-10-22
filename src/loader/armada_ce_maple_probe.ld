/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_armada_ce_bind_probe)
PHDRS { probe PT_LOAD FLAGS(7); }
SECTIONS {
    . = 0x8c004000;
    .entry : { KEEP(*(.entry)) } :probe
    .text : { *(.text .text.*) } :probe
    .rodata : {
        KEEP(*(.rodata.kui_armada_ce_maple_probe_contract))
        *(.rodata .rodata.*)
    } :probe
    .data : { *(.data .data.*) } :probe
    . = ALIGN(4);
    __armada_bind_binary_end = .;
    .bss (NOLOAD) : {
        __armada_bind_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __armada_bind_bss_end = .;
    } :probe
    ASSERT(_kui_armada_ce_bind_probe == 0x8c004000, "Maple observer entry moved")
    ASSERT(__armada_bind_bss_end <= 0x8c007c00, "Maple observer exceeds low window")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
