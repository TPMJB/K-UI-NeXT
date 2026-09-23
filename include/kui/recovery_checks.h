/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RECOVERY_CHECKS_H
#define KUI_RECOVERY_CHECKS_H
#include <stddef.h>
#include <stdint.h>

#define KUI_RECOVERY_RAW_BYTES 2352u
/* One byte encodes minute tens in its upper nibble, including GD-ROM's
 * extended minute values above 99. Reject values that would wrap that byte. */
#define KUI_RECOVERY_FAD_MAX 719999u

enum kui_recovery_sector_flags {
    KUI_RECOVERY_SECTOR_SYNC = 1u,
    KUI_RECOVERY_SECTOR_ADDRESS = 2u,
    KUI_RECOVERY_SECTOR_EDC = 4u,
    KUI_RECOVERY_SECTOR_ECC = 8u,
    KUI_RECOVERY_SECTOR_UNSUPPORTED = 16u,
    KUI_RECOVERY_SECTOR_INPUT = 32u,
    KUI_RECOVERY_SECTOR_RESERVED = 64u
};

/* Replace an equal-length region's contribution to a finalized IEEE CRC32.
 * whole is the old full-track CRC; before/after are the old/new region CRCs;
 * suffix_bytes is the number of bytes AFTER that region. This does not read
 * bytes, validate a baseline, or establish that a patch reached storage.
 * Caller must have that evidence; unequal-length replacement is unsupported.
 * Same CRC convention as kui_crc32 and zlib.crc32. */
uint32_t kui_recovery_crc_replace(uint32_t whole,uint32_t before,uint32_t after,
    uint64_t suffix_bytes);

/* Read-only validation of exactly one raw Mode 1 sector: sync, expected FAD,
 * EDC, the zero-reserved field and both P/Q parity sets. Returns zero only when every supported check
 * passes. Caller must already know this is a data track: arbitrary audio
 * cannot be classified from its contents alone and is not validated here.
 * Mode 2 is unsupported. NULL, wrong byte count, or FAD outside
 * [150,KUI_RECOVERY_FAD_MAX] is INPUT.
 * Does not repair/synthesize bytes or identify which bytes caused a mismatch.
 *
 * These backend helpers lazily initialize bounded tables. Like the current
 * drive/storage APIs they are for one serialized worker, not concurrent calls.
 * The saved-file Advanced CRC app uses sector checks explicitly; healthy
 * acquisition still uses its existing fast checks and does not call these. */
unsigned kui_recovery_sector_check(const void *raw,size_t size,uint32_t fad);

#endif
