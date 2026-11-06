/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_LATE_H
#define KUI_VC2_CE_VMU_LATE_H
#include "kui/vc2_ce_vmu_handoff.h"
#include <stdbool.h>

/* The complete prepared helper occupies exactly96 raw card blocks. Its one
 * or two physical FAT runs are retained in three retired boot scalars; the
 * game's track slots, extent slots and complete TOC are never changed. */
#define KUI_VC2_VMU_LATE_BLOCKS (KUI_VC2_VMU_RESERVE_BYTES/512u)
typedef uint32_t (*kui_vc2_vmu_late_read_block)(uint32_t,uint8_t[512]);

/* Stage admission, before copying these immutable scalars into low memory.
 * All byte/range checks complete before any helper read. Runtime calls the
 * smaller read loop only after this exact admission succeeds. A zero second
 * LBA denotes the single96-block run, never a second file extent. */
static inline bool kui_vc2_vmu_late_map_valid(uint32_t first,uint32_t split,
    uint32_t second,uint64_t partition_start,uint64_t partition_end) {
    if(!split || split>KUI_VC2_VMU_LATE_BLOCKS ||
       partition_start>=partition_end || partition_end>UINT64_C(0x100000000))return false;
    const uint64_t first_end=(uint64_t)first+split;
    if(first<partition_start || first_end>partition_end)return false;
    if(split==KUI_VC2_VMU_LATE_BLOCKS)return !second;
    const uint64_t second_end=(uint64_t)second+KUI_VC2_VMU_LATE_BLOCKS-split;
    return second && second>=partition_start && second_end<=partition_end &&
        (first_end<=second || second_end<=first);
}

/* No filesystem, callbacks retained from K-UI, intermediate sector buffer,
 * initializer or SD write. Caller supplies a live resident raw-block reader
 * and an exactly48KiB destination, normally the fixed high P2 range.
 * The three scalars must remain unchanged after map_valid. On error,
 * return the raw reader status and the failing physical LBA; output beyond
 * that reader's last attempted 512-byte block remains untouched. Caller
 * must compare the newly loaded immutable CRC before entering the helper. */
static inline uint32_t kui_vc2_vmu_late_read(uint32_t first,uint32_t split,
    uint32_t second,uint8_t *destination,kui_vc2_vmu_late_read_block read,uint32_t *failed_lba) {
    uint32_t lba=first;
    for(uint32_t block=0u;block<KUI_VC2_VMU_LATE_BLOCKS;++block) {
        if(block==split)lba=second;
        const uint32_t status=read(lba,destination);
        if(status) {*failed_lba=lba;return status;}
        ++lba;destination+=512u;
    }
    return 0u;
}
#endif
