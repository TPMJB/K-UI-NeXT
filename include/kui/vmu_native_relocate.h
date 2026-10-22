/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_RELOCATE_H
#define KUI_VMU_NATIVE_RELOCATE_H
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_NATIVE_RELOCATE_HEADER_BYTES 72u
#define KUI_VMU_NATIVE_RELOCATE_RECORD_BYTES 8u
#define KUI_VMU_NATIVE_RELOCATE_MAX_SPAN 0x10000u
/* Explicit LE wire: magic "KUIVRL01"; version/header/image/span/link/count/
 * wire/CRC32 words at8..36; exact image SHA256 at40; sorted records
 * (image offset, original pointer word) at72. Produced from --emit-relocs.
 * CRC32 covers the complete wire with bytes36..39 treated as zero.
 * This is embedded build metadata, not a cache granting hook authority. */
enum kui_vmu_native_relocate_result {
    KUI_VMU_NATIVE_RELOCATED=0,
    KUI_VMU_NATIVE_RELOCATE_INVALID=-1,
    KUI_VMU_NATIVE_RELOCATE_TABLE=-2,
    KUI_VMU_NATIVE_RELOCATE_IMAGE=-3,
    KUI_VMU_NATIVE_RELOCATE_OVERLAP=-4
};
/* Copy initialized bytes and relocate explicit internal pointer words only.
 * All validation precedes every destination write; failure leaves it intact.
 * Destination must be a separately proven allocation of at least span bytes,
 * addressed by a32-byte-aligned canonical P1 load address. Original pointer
 * U0/P1/P2 aliases are retained, including legitimate one-past-end pointers.
 * Caller owns cache publication and BSS/stack initialization. No MMIO, game
 * allocator calls or executable hook writes occur here. */
enum kui_vmu_native_relocate_result kui_vmu_native_relocate(
    const void *image,size_t image_bytes,const void *table,size_t table_bytes,
    void *destination,size_t allocation_bytes,uint32_t load_address);
#endif
