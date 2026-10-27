/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_READAHEAD_CACHE_H
#define KUI_NATIVE_READAHEAD_CACHE_H
#include "kui/retail_image.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_NATIVE_RA_CACHE_BLOCKS 384u
#define KUI_NATIVE_RA_CACHE_BLOCK_BYTES 512u
#define KUI_NATIVE_RA_CACHE_BYTES (KUI_NATIVE_RA_CACHE_BLOCKS * KUI_NATIVE_RA_CACHE_BLOCK_BYTES)

/* One contiguous physical-card interval. data belongs to the admitted lease,
 * not a guest destination. A valid entry is published only after all512bytes
 * have been copied. Calls are serialized by the reader's existing mask/lock.
 * The caller supplies only normalized, CRC-checked blocks from stream_take. */
struct kui_native_ra_cache {
    uint8_t *data;
    uint32_t base,head,count,end;
};

/* Exact192KiB,32-byte aligned arena, disjoint from the metadata. No arena
 * clearing or IO occurs. Invalid arguments preserve the previous metadata. */
bool kui_native_ra_cache_init(struct kui_native_ra_cache *,void *data,size_t bytes);
/* Discard validity and select [base,end), end exclusive. The geometry must
 * fit uint32_t; a card extent ending at2^32 is conservatively not prefetched. */
bool kui_native_ra_cache_rebase(struct kui_native_ra_cache *,uint32_t base,uint32_t end);
/* On a hit discard only blocks BEFORE lba. The requested block remains valid
 * for adjacent raw-sector requests sharing one physical512-byte boundary.
 * A miss never changes the window. A returned pointer is valid only until
 * the next serialized mutation; it must not be retained by a guest. */
const uint8_t *kui_native_ra_cache_lookup(struct kui_native_ra_cache *,uint32_t lba);
/* Append exactly the next contiguous checked block, within the admitted
 * extent and available ring capacity. Source and destination may overlap. */
bool kui_native_ra_cache_append(struct kui_native_ra_cache *,uint32_t lba,const uint8_t checked[512]);
bool kui_native_ra_cache_next(const struct kui_native_ra_cache *,uint32_t *lba);

/* Recheck the bounded64-slot retained map's track/extent geometry, then
 * return the end of the physical extent containing lba. No request cursor
 * length authorizes prefetch. Invalid or ambiguous ownership returns0.
 * Legal shared-container boundary blocks receive the smaller owner bound. */
uint32_t kui_native_ra_extent_end(const struct kui_retail_manifest *,uint32_t lba);
#endif
