/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_MENU_CAPSULE_H
#define KUI_SMASH_MENU_CAPSULE_H
#include "kui/retail_image.h"
#include "kui/vmu_persist_profile.h"
#include <stdbool.h>

#define KUI_SMASH_MENU_CAPSULE_PATH "0:/KUI/apps/games/smash-menu-native.cap"
#define KUI_SMASH_MENU_OUTER_CAPSULE_PATH "0:/KUI/apps/games/smash-menu-outer.cap"
#define KUI_SMASH_MENU_VC2_CAPSULE_PATH "0:/KUI/apps/games/smash-menu-vc2.cap"
#define KUI_SMASH_MENU_SWIRL_CAPSULE_PATH "0:/KUI/apps/games/smash-menu-swirl.cap"

/* A read-only launch capsule is a sector-padded prepared module payload.
 * Its physical map is independent of the filesystem once the menu starts. */
struct kui_smash_menu_capsule_runs {
    uint32_t first_lba,first_blocks,second_lba,blocks;
};
struct kui_smash_menu_capsule_prepared {
    struct kui_smash_menu_capsule_runs runs;
    struct kui_smash_menu_capsule_runs allocated_runs;
    uint32_t payload_bytes,bytes,crc32,seed_offset;
};
/* FatFs CREATE_LINKMAP table, with all geometry relative to the partition
 * except volume_first. Only the declared file blocks become readable. Every
 * cluster run is checked, including allocation slack after the file's end.
 * Output is cleared on failure. No filesystem or device operations occur. */
bool kui_smash_menu_capsule_map(const uint32_t *table,size_t words,
    uint32_t data_sector,uint32_t sectors_per_cluster,uint32_t cluster_limit,
    uint32_t volume_first,uint32_t volume_blocks,uint32_t file_blocks,
    struct kui_smash_menu_capsule_runs *out);
/* Reject overlap with every disc extent and the complete allocated backing
 * files, including their cluster slack. Both inputs must already be valid. */
bool kui_smash_menu_capsule_disjoint(const struct kui_smash_menu_capsule_runs *,
    const struct kui_retail_manifest *,const struct kui_vmu_persist_profile *);
/* Both maps describe complete allocated capsule clusters, not merely useful
 * file blocks. Invalid or overlapping maps are refused. */
bool kui_smash_menu_capsule_runs_disjoint(const struct kui_smash_menu_capsule_runs *,
    const struct kui_smash_menu_capsule_runs *);
#endif
