/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(5); }
SECTIONS {
 . = 0x8c010000;
 .entry : { KEEP(*(.entry)) } :image
 ASSERT(SIZEOF(.entry)<=0x100,"retail entry exceeds header")
 . = 0x8c010100;
 .package_header : { KEEP(*(.package_header)) } :image
 ASSERT(SIZEOF(.package_header)==64,"retail header must be 64 bytes")
 . = 0x8c011000;
 .retail_map : { KEEP(*(.retail_map)) } :image
 ASSERT(SIZEOF(.retail_map)==4096,"retail map must be 4096 bytes")
 . = 0x8c012000;
 .stage_blob : { KEEP(*(.stage_blob)) } :image
 ASSERT(SIZEOF(.stage_blob)>0 && SIZEOF(.stage_blob)<=0x100000,"retail stage bounds")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
