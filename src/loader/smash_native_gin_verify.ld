/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_smash_native_gin_verify)
PHDRS { image PT_LOAD FLAGS(5); }
SECTIONS {
 . = 0x8cc00300;
 .text : { KEEP(*(.text.kui_smash_native_gin_verify)) *(.text .text.*) } :image
 .rodata : { *(.rodata .rodata.*) } :image
 .data : { *(.data .data.*) } :image
 __smash_native_gin_verify_end = .;
 ASSERT(.<=0x8cc01000,"native GIN verifier exceeds capsule prefix")
 .bss (NOLOAD) : { *(.bss .bss.* COMMON) } :image
 ASSERT(SIZEOF(.bss)==0,"native GIN verifier must have no mutable globals")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
