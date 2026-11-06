/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_CE_HELPER_H
#define KUI_SMASH_CE_HELPER_H
#include <stddef.h>
#include <stdint.h>
#define KUI_SMASH_CE_HELPER_MAGIC0 UINT32_C(0x5349554b)
#define KUI_SMASH_CE_HELPER_MAGIC1 UINT32_C(0x31484543)
#define KUI_SMASH_CE_HELPER_HEADER_BYTES 32u
#define KUI_SMASH_CE_HELPER_PADDED_BYTES 49152u
struct kui_smash_ce_helper {
    const uint8_t *raw;
    uint32_t raw_bytes,padded_bytes,stored_bytes;
};
/* A complete typed header precedes its raw helper bytes. Version1 stores the
 * immutable helper alone; version2 stores the complete checked return carrier
 * while retaining raw_bytes as the exact unchanged helper prefix. A magic pair in a
 * literal pool is not a header. 1 unique valid record,0 absent,-1 malformed
 * or duplicate. Output clears on failure, except invalid overlap with input.
 * This locates bytes only; the caller must verify the complete padded CRC
 * against the independently admitted module owner and late-load seed. Use
 * stored_bytes when copying the admitted embedded file into a fresh capsule. */
int kui_smash_ce_helper_find(const void *module,size_t bytes,unsigned kind,
    struct kui_smash_ce_helper *out);
#endif
