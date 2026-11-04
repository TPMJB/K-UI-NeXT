/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_REPORT_STAGE_CACHE_H
#define KUI_REPORT_STAGE_CACHE_H
#include "kui/vmu_fs_admission.h"
/* Stage-only metadata batching. Two8-sector ways keep mirrored FAT scans
 * resident together; at most8KiB is read-ahead, never into protected game
 * extents. Every admission starts with a fresh zeroed cache and fresh proof. */
struct kui_report_stage_cache {
    uint8_t bytes[2][8u*512u];
    uint64_t card_sectors,partition_first,partition_end;
    const struct kui_vmu_fs_range *forbidden;
    size_t forbidden_count;
    int (*read_multi)(void *,uint32_t,uint32_t,uint8_t *);
    void *context;
    uint32_t first[2],count[2],recent;
    uint32_t requests,wire_calls,physical_sectors,hits,poisoned,initialized;
};
int kui_report_stage_cache_init(struct kui_report_stage_cache *,uint64_t card_sectors,
    uint64_t partition_first,uint64_t partition_end,const struct kui_vmu_fs_range *,size_t,
    int (*read_multi)(void *,uint32_t,uint32_t,uint8_t *),void *context);
/* Callback-compatible single-sector interface. LBA0 is single-sector only;
 * all other requests must lie in the admitted partition. Failure flushes
 * both ways permanently and leaves out unchanged. Physical reads and logical
 * requests are each capped at the original ownership walk's70,000 sectors. */
int kui_report_stage_cache_read(void *,uint32_t lba,uint8_t out[512]);
#endif
