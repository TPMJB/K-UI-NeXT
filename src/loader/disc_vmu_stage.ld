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
 ASSERT(SIZEOF(.trampoline)==128,"physical VMU trampoline must be128bytes")
 . = ALIGN(4);
 __disc_vmu_stage_binary_end = .;
 ASSERT(.<=0x8cf00000,"physical VMU stage binary exceeds1MiB")
 .bss (NOLOAD) : {
  __disc_vmu_stage_bss_begin = .;
  *(.bss .bss.* COMMON)
  . = ALIGN(32);
  __disc_vmu_stage_bss_end = .;
 } :image
 ASSERT(.<=0x8cfe0000,"physical VMU stage workspace overlaps high stack")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
