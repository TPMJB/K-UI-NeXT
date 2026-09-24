/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(5); }
SECTIONS
{
    . = 0x8c010000;
    .entry : { KEEP(*(.entry)) } :image
    ASSERT(SIZEOF(.entry) <= 0x100, "loader entry exceeds header offset")
    . = 0x8c010100;
    .package_header : { KEEP(*(.package_header)) } :image
    ASSERT(SIZEOF(.package_header) == 64, "loader package header must be 64 bytes")
    . = 0x8c011000;
    .loader_manifest : { KEEP(*(.loader_manifest)) } :image
    ASSERT(SIZEOF(.loader_manifest) == 1600, "loader manifest must be 1600 bytes")
    . = 0x8c012000;
    .resident_blob : { KEEP(*(.resident_blob)) } :image
    ASSERT(SIZEOF(.resident_blob) > 0, "resident binary is empty")
    ASSERT(SIZEOF(.resident_blob) <= 0x100000, "resident binary exceeds relocation bounds")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
