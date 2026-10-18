/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_FS_ADMISSION_H
#define KUI_VMU_FS_ADMISSION_H
#include "kui/vmu_persist_profile.h"

#define KUI_VMU_FS_MAX_DEPTH 16u
#define KUI_VMU_FS_MAX_ENTRIES 65536u
#define KUI_VMU_FS_MAX_DIRECTORY_SECTORS 4096u
#define KUI_VMU_FS_MAX_DIRECTORY_CLUSTERS 8192u
#define KUI_VMU_FS_MAX_FAT_ENTRIES 4194304u
#define KUI_VMU_FS_MAX_READS 70000u
#define KUI_VMU_FS_WORK_BYTES 49152u

enum kui_vmu_fs_result {
    KUI_VMU_FS_OK=0, KUI_VMU_FS_ARGUMENT=-1, KUI_VMU_FS_IO=-2,
    KUI_VMU_FS_VOLUME=-3, KUI_VMU_FS_UNSUPPORTED=-4,
    KUI_VMU_FS_CORRUPT=-5, KUI_VMU_FS_LIMIT=-6,
    KUI_VMU_FS_PATH=-7, KUI_VMU_FS_ALLOCATION=-8,
    KUI_VMU_FS_CROSSLINK=-9, KUI_VMU_FS_IDENTITY=-10,
    KUI_VMU_FS_CHANGED=-11, KUI_VMU_FS_PROTECTED=-12
};
struct kui_vmu_fs_range {uint64_t lba,blocks;};
/* Caller owns the initialized card and serializes all I/O for this entire
 * operation. CID/capacity are freshly probed on that SAME read-only device;
 * this generic helper cannot establish bus or pointer ownership. partition_end
 * is exclusive. pair_path is canonical /KUI/vmu-test/pNNNN (0001..9999).
 * Optional protected ranges include every game/audio allocation; target whole
 * clusters, including slack, must not intersect any range. No write callback,
 * allocation, filesystem mount, journal recovery or SD initialization exists. */
struct kui_vmu_fs_input {
    int (*read_sector)(void *,uint32_t lba,uint8_t out[512]);
    void *context;
    uint64_t card_sectors,partition_first,partition_end;
    uint32_t high_capacity;
    uint8_t cid[16],image_id[16];
    const char *pair_path;
    const struct kui_vmu_fs_range *forbidden;
    size_t forbidden_count;
};
enum kui_vmu_fs_boot_reject {
    KUI_VMU_FS_BOOT_ZERO=1u, KUI_VMU_FS_BOOT_REVISION=2u,
    KUI_VMU_FS_BOOT_SECTOR_SHIFT=4u, KUI_VMU_FS_BOOT_CLUSTER_SHIFT=8u,
    KUI_VMU_FS_BOOT_FAT_COUNT=16u, KUI_VMU_FS_BOOT_FLAGS=32u,
    KUI_VMU_FS_BOOT_PARTITION_OFFSET=64u, KUI_VMU_FS_BOOT_VOLUME_LENGTH=128u
};
/* Diagnostics from the main VBR bytes already read by admission. valid=1
 * means an exFAT main VBR was recognized, not that it passed admission.
 * No additional reads, changes to the card, or write authority are implied. */
struct kui_vmu_fs_boot_diagnostics {
    uint32_t valid,reject_mask;
    uint64_t partition_offset,volume_sectors;
    uint32_t revision,flags,sector_shift,cluster_shift,fat_count,zero_or;
};
struct kui_vmu_fs_stats {
    uint32_t reads,directory_sectors,entries,directory_clusters,fat_entries;
    uint32_t target_clusters,last_lba,last_cluster,last_role;
    int32_t result;
    uint32_t detail;
    struct kui_vmu_fs_boot_diagnostics boot;
};
/* Exclusive caller-owned scratch, zeroed by each API. Internal storage has
 * max_align_t alignment; it is never retained after return. Diagnostics remain
 * available after refusal. A fresh invocation may retry in the shell; no
 * partial output or write authority escapes a failed scan. */
struct kui_vmu_fs_workspace {
    struct kui_vmu_fs_stats stats;
    union {max_align_t alignment;uint8_t bytes[KUI_VMU_FS_WORK_BYTES];} private_work;
};
/* Derive live geometry, exact paths/locators, useful/full-cluster maps and
 * global ownership of BASE/JOURNAL/IDENTITY. Files have exact lengths
 * 131072/1024/16 and identity bytes must equal input.image_id. Content hashes
 * remain zero: collect_snapshot is a separate, read-only next step. On every
 * failure out is zero. All bounded scans must finish; limit exhaustion refuses.
 * No unrelated large-file payload is read. Unsupported metadata is refused.
 * The caller must not alias input/output/workspace or callbacks' buffers. */
int kui_vmu_fs_collect(const struct kui_vmu_fs_input *,
    struct kui_vmu_persist_profile *out,struct kui_vmu_fs_workspace *);
/* Freshly derive ownership, then compare against a structurally valid earlier
 * record. Provenance hashes/CRCs/journal sequence are deliberately ignored.
 * This authorizes neither recovery nor writing and does not alter expected. */
int kui_vmu_fs_admit(const struct kui_vmu_fs_input *,
    const struct kui_vmu_persist_profile *expected,struct kui_vmu_fs_workspace *);
#endif
