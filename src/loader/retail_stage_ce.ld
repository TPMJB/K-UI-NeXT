/* SPDX-License-Identifier: GPL-3.0-only */
/* The Windows CE probe stage: retail_stage.ld linked 64 KiB higher, so CE's
 * boot prefix destination [0x8ce01000,0x8ce01800) lies outside the stage. */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(7); }
SECTIONS {
 . = 0x8ce10000;
 .entry : { KEEP(*(.entry)) } :image
 .text : { *(.text .text.*) } :image
 .rodata : { *(.rodata .rodata.*) } :image
 .data : { *(.data .data.*) } :image
 .trampoline : { KEEP(*(.trampoline)) } :image
 ASSERT(SIZEOF(.trampoline)==128,"retail trampoline must be 128 bytes")
 ASSERT(__retail_trampoline_code_end-__retail_trampoline_start<=64,"CE entry trampoline code must fit in 64 bytes")
 .resident_blob : { KEEP(*(.resident_blob)) } :image
 .armada_bind_blob : { KEEP(*(.armada_bind_blob)) } :image
 .armada_bind_stage_contract : { KEEP(*(.armada_bind_stage_contract)) } :image
 .armada_excerpt_seed : { KEEP(*(.armada_excerpt_seed)) } :image
 .armada_excerpt_stage_contract : { KEEP(*(.armada_excerpt_stage_contract)) } :image
 ASSERT(SIZEOF(.armada_bind_blob)==0 || DEFINED(_kui_armada_ce_bind_stage_contract),
        "Armada binding blob requires the admitted C stage installer")
 . = ALIGN(4);
 __retail_stage_binary_end = .;
 ASSERT(.<=0x8cf10000,"retail stage binary too large")
 .bss (NOLOAD) : {
  __retail_stage_bss_begin = .;
  *(.bss .bss.* COMMON)
  . = ALIGN(4);
  __retail_stage_bss_end = .;
 } :image
 ASSERT(.<=0x8cfe0000,"retail stage overlaps high stack")
 ASSERT(!DEFINED(_kui_armada_ce_excerpt_stage_contract) || .<0x8cf50000,
        "Armada excerpt stage/BSS overlaps reserved PCM pool")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
