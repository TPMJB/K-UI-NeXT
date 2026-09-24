/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(7); }
SECTIONS {
 . = 0x8ce00000;
 .entry : { KEEP(*(.entry)) } :image
 .text : { *(.text .text.*) } :image
 .rodata : { *(.rodata .rodata.*) } :image
 .data : { *(.data .data.*) } :image
 .trampoline : { KEEP(*(.trampoline)) } :image
 ASSERT(SIZEOF(.trampoline)==128,"retail trampoline must be 128 bytes")
 .resident_blob : { KEEP(*(.resident_blob)) } :image
 . = ALIGN(4);
 __retail_stage_binary_end = .;
 ASSERT(.<=0x8cf00000,"retail stage binary too large")
 .bss (NOLOAD) : {
  __retail_stage_bss_begin = .;
  *(.bss .bss.* COMMON)
  . = ALIGN(4);
  __retail_stage_bss_end = .;
 } :image
 ASSERT(.<=0x8cfe0000,"retail stage overlaps high stack")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
