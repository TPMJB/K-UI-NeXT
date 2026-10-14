/* SPDX-License-Identifier: GPL-3.0-only */
/* P1 executable/state allocation within Armada's existing reserved 512 KiB.
 * The PCM/ring occupies [8cf50040,8cfa6260); no low forwarding stub is used. */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_armada_ce_bind_probe)
PHDRS { probe PT_LOAD FLAGS(7); }
SECTIONS {
    . = 0x8cfa7000;
    .entry : { KEEP(*(.entry)) } :probe
    .text : { *(.text .text.*) } :probe
    .rodata : { *(.rodata .rodata.*) } :probe
    .data : { *(.data .data.*) } :probe
    . = ALIGN(4);
    .armada_stream_contract : {
        __armada_stream_contract = .;
        LONG(0x31444341); /* ACD1: authored controller layout, version1 */
        LONG(1);
        LONG(0x8cfa7000);
        LONG(__armada_bind_binary_end);
        LONG(__armada_bind_bss_begin);
        LONG(__armada_bind_bss_end);
        LONG(0x8cfbf000);
        LONG(_kui_armada_ce_bind_probe);
    } :probe
    __armada_bind_binary_end = .;
    .bss (NOLOAD) : {
        __armada_bind_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __armada_bind_bss_end = .;
    } :probe
    ASSERT(_kui_armada_ce_bind_probe == 0x8cfa7000, "stream controller entry moved")
    ASSERT(SIZEOF(.armada_stream_contract) == 32, "stream controller footer changed")
    ASSERT(__armada_stream_contract + 32 == __armada_bind_binary_end,
           "stream controller footer must finish initialized bytes")
    ASSERT(__armada_bind_bss_begin == __armada_bind_binary_end,
           "stream controller BSS must follow initialized bytes")
    ASSERT(__armada_bind_bss_end <= 0x8cfbf000, "stream controller exceeds reserved code/state budget")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
