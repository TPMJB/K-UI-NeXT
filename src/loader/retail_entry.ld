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
 . = 0x8c010180;
 .vmu_launch_header : { KEEP(*(.vmu_launch_header)) } :image
 ASSERT(SIZEOF(.vmu_launch_header)==0 || SIZEOF(.vmu_launch_header)==64,"VMU launch capability must be 64 bytes")
 . = 0x8c0101c0;
 .cold_image_header : { KEEP(*(.cold_image_header)) } :image
 ASSERT(SIZEOF(.cold_image_header)==0 || SIZEOF(.cold_image_header)==24,"Cold image capability must be 24 bytes")
 . = 0x8c0101e0;
 .smash_native_gin_capability : { KEEP(*(.smash_native_gin_capability)) } :image
 ASSERT(SIZEOF(.smash_native_gin_capability)==0 || SIZEOF(.smash_native_gin_capability)==16,"native GIN capability must be16 bytes")
 .smash_ce_return_capability : { KEEP(*(.smash_ce_return_capability)) } :image
 ASSERT(SIZEOF(.smash_ce_return_capability)==0 || SIZEOF(.smash_ce_return_capability)==16,"CE return capability must be16 bytes")
 ASSERT(SIZEOF(.smash_native_gin_capability)==0 || SIZEOF(.smash_ce_return_capability)==0,"Smash native and CE capabilities overlap")
 . = 0x8c010200;
 .smash_ce_gin_header : { KEEP(*(.smash_ce_gin_header)) } :image
 ASSERT(SIZEOF(.smash_ce_gin_header)==0 || SIZEOF(.smash_ce_gin_header)==160,"CE GIN state must be 160 bytes")
 .smash_native_gin_header : { KEEP(*(.smash_native_gin_header)) } :image
 ASSERT(SIZEOF(.smash_native_gin_header)==0 || SIZEOF(.smash_native_gin_header)==160,"native GIN state must be 160 bytes")
 ASSERT(SIZEOF(.smash_ce_gin_header)==0 || SIZEOF(.smash_native_gin_header)==0,"GIN continuation headers overlap")
 . = 0x8c0102a0;
 .collection_session_header : { KEEP(*(.collection_session_header)) } :image
 ASSERT(SIZEOF(.collection_session_header)==0 || SIZEOF(.collection_session_header)==96,"Collection session receipt must be96 bytes")
 . = 0x8c010300;
 .smash_native_gin_verify : { KEEP(*(.smash_native_gin_verify)) } :image
 ASSERT(SIZEOF(.smash_native_gin_verify)<=0xd00,"native GIN verifier exceeds prefix budget")
 . = 0x8c011000;
 .retail_map : { KEEP(*(.retail_map)) } :image
 ASSERT(SIZEOF(.retail_map)==4096,"retail map must be 4096 bytes")
 . = 0x8c012000;
 .stage_blob : { KEEP(*(.stage_blob)) } :image
 ASSERT(SIZEOF(.stage_blob)>0 && SIZEOF(.stage_blob)<=0x100000,"retail stage bounds")
 ASSERT(SIZEOF(.smash_native_gin_header)==0 || SIZEOF(.stage_blob)<=0xfe000,"native GIN capsule overlaps transfer scratch")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
