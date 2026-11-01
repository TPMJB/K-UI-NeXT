/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(5); }
SECTIONS {
 . = 0x8c010000;
 .entry : { KEEP(*(.entry)) } :image
 ASSERT(SIZEOF(.entry)<=0x100,"disc entry overlaps capability")
 . = 0x8c010100;
 .package_header : { KEEP(*(.package_header)) } :image
 ASSERT(SIZEOF(.package_header)==64,"disc capability size")
 . = 0x8c011000;
 .disc_config : { KEEP(*(.disc_config)) } :image
 ASSERT(SIZEOF(.disc_config)==512,"disc config size")
 . = 0x8c012000;
 .stage_blob : { KEEP(*(.stage_blob)) } :image
 ASSERT(SIZEOF(.stage_blob)>0 && SIZEOF(.stage_blob)<=0x10000,"disc stage size")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
