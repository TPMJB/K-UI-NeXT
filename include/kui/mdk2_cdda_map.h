/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MDK2_CDDA_MAP_H
#define KUI_MDK2_CDDA_MAP_H
#include "kui/retail_image.h"
#include "kui/retail_loader_layout.h"
#include "kui/mdk2_cdda_layout.h"
#include <stdbool.h>
#include <stdint.h>

/* The temporary source shares the installer's audited lifetime: owner BSS
 * clearing stops below the high stage and the real allocator is guarded
 * before its first ordinary allocation. It is never a final reservation. */
static inline bool kui_mdk2_cdda_stage_map_address(uint32_t address) {
    return !(address&3u) && address>=KUI_RETAIL_STAGE_ADDRESS &&
        address<=UINT32_C(0x8cfcffe0)-sizeof(struct kui_retail_manifest);
}
static inline bool kui_mdk2_cdda_owned_map_address(uint32_t address) {
    return address==KUI_MDK2_CDDA_MAP_ALIAS &&
        sizeof(struct kui_retail_manifest)<=KUI_MDK2_CDDA_MAP_BYTES;
}
static inline bool kui_mdk2_cdda_map_overlaps(uint32_t map,uint32_t address,uint32_t bytes) {
    uint32_t canonical=map>=UINT32_C(0xac000000) && map<UINT32_C(0xad000000)?
        map-UINT32_C(0x20000000):map;
    return bytes && (uint64_t)address<canonical+sizeof(struct kui_retail_manifest) &&
        (uint64_t)address+bytes>canonical;
}

/* The installer calls this under its inherited BL/IMASK mask after copying
 * and comparing the owned map. The reader preserves every existing handle
 * and only changes its immutable table pointers. */
int kui_mdk2_cdda_manifest_bind(const struct kui_retail_manifest *temporary,
                              const struct kui_retail_manifest *owned);
#endif
