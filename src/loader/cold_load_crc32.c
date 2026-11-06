/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cold_load.h"
#if KUI_COLD_CRC32_SHARED_RETAIL
#include "ce_return_crc32.h"
#else
#include "stage_fast_crc32.h"
#endif

/* Kept out of line so the cold scope can select its real P1 code alias.
 * The small live resident retains its existing CRC implementation. */
uint32_t __attribute__((noipa)) kui_cold_load_crc32_leaf(uint32_t previous,
    const void *data,size_t bytes) {
#if KUI_COLD_CRC32_SHARED_RETAIL
    return kui_ce_return_crc32(previous,data,bytes);
#else
    return kui_stage_crc32(previous,data,bytes);
#endif
}
#if KUI_COLD_CRC32_SHARED_RETAIL
/* The bounded CE return image needs both public CRC entrypoints. Share its
 * existing byte-indexed implementation instead of linking a second nibble-table
 * implementation. No checksum, callback scope or resident reader changes. */
uint32_t kui_retail_crc32(uint32_t previous,const void *data,size_t bytes) {
    return kui_cold_load_crc32_leaf(previous,data,bytes);
}
#endif
uint32_t kui_cold_load_crc32(uint32_t previous,const void *data,size_t bytes) {
    return kui_cold_load_call3((uintptr_t)kui_cold_load_crc32_leaf,
        previous,(uintptr_t)data,bytes);
}
