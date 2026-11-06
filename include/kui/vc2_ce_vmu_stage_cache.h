/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_STAGE_CACHE_H
#define KUI_VC2_CE_VMU_STAGE_CACHE_H
#include "kui/vmu_persist_profile.h"
#include <stdbool.h>
#define KUI_VC2_STAGE_FAT_BLOCKS 32u
#define KUI_VC2_STAGE_READ_BLOCKS 32u
#define KUI_VC2_STAGE_MEMO_BLOCKS 256u
/* Temporary cold-stage memory only. The caller exclusively owns the same
 * device for admission, recovery and validation. It invalidates before EVERY
 * write or sync; no cached byte grants write authority or skips admission. */
struct kui_vc2_ce_vmu_stage_cache {
    /* Admission and validation have disjoint epochs. Reuse the snapshot
     * storage for exact-sector metadata only during read-only admission. */
    _Alignas(32) uint8_t base[KUI_VC2_STAGE_MEMO_BLOCKS][512];
    /* CE admission uses two 32-sector FAT windows. Native metadata-only
     * admission reuses the same bytes as four 16-sector verified windows. */
    _Alignas(32) uint8_t fat[2][KUI_VC2_STAGE_FAT_BLOCKS][512];
    /* Core metadata scratch is not guaranteed 32-byte aligned. Always use
     * this owned receiver so a directory/bitmap read can take the existing
     * block-DMA path rather than silently falling back to byte PIO. */
    _Alignas(32) uint8_t single[512];
    uint32_t base_valid,fat_lba[2],fat_blocks[2];
    uint32_t memo_lba[KUI_VC2_STAGE_MEMO_BLOCKS];
    uint32_t memo_valid[KUI_VC2_STAGE_MEMO_BLOCKS/32u];
    /* Four verified-prefix lengths fit the former alignment padding. The
     * complete temporary cache remains 165472 bytes. Only mode 3 uses them. */
    uint32_t memo_fat_blocks[4],mode;
    uint32_t requests,hits,commands,sectors,base_loads;
};
typedef int (*kui_vc2_stage_read_run)(void *,uint32_t,uint32_t,void *);
void kui_vc2_ce_vmu_stage_cache_invalidate(struct kui_vc2_ce_vmu_stage_cache *);
/* base_snapshot is true only after fresh semantic admission and completed
 * redo recovery. FAT windows and exact-sector metadata are enabled only in
 * read-only admission. The owning stage validates profile and invalidates
 * every identity, write and sync boundary. Cached bytes never skip a semantic
 * ownership or image validation check. */
int kui_vc2_ce_vmu_stage_cache_read(struct kui_vc2_ce_vmu_stage_cache *,
    const struct kui_vmu_persist_profile *,bool fat_windows,bool base_snapshot,
    uint64_t,void *,kui_vc2_stage_read_run,void *);
/* Native/disc callers already have a FAT scanner selected from their fresh
 * live VBR. Their non-FAT reads use exact memoization and live boot batching,
 * without enabling prepared-profile FAT read-ahead. */
int kui_vc2_ce_vmu_stage_cache_metadata_read(struct kui_vc2_ce_vmu_stage_cache *,
    const struct kui_vmu_persist_profile *,bool admission,uint64_t,void *,
    kui_vc2_stage_read_run,void *);
/* Native metadata-admission-only exact byte reuse. The caller's own live-VBR
 * scanner selected the physical window. Peek is 1 hit, 0 miss, -1 invalid arguments;
 * no output changes on a miss/refusal. Store accepts ONLY a CRC-verified
 * prefix after confirmed healthy stream teardown, never an uncertain run.
 * These functions perform no read-ahead and confer no ownership authority. */
int kui_vc2_ce_vmu_stage_cache_admission_peek(struct kui_vc2_ce_vmu_stage_cache *,
    const struct kui_vmu_persist_profile *,uint64_t,void *);
int kui_vc2_ce_vmu_stage_cache_admission_store(struct kui_vc2_ce_vmu_stage_cache *,
    const struct kui_vmu_persist_profile *,uint32_t,uint32_t,const void *);
#endif
