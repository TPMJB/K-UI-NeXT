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
 . = ALIGN(4);
 __disc_stage_binary_end = .;
 ASSERT(.<=0x8ce10000,"disc stage binary too large")
 .bss (NOLOAD) : {
  __disc_stage_bss_begin = .;
  *(.bss .bss.* COMMON)
  . = ALIGN(4);
  __disc_stage_bss_end = .;
 } :image
 ASSERT(.<=0x8ce20000,"disc stage workspace too large")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
