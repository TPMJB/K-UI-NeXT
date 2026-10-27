/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_SAVE_SNAPSHOT_H
#define KUI_VMU_SAVE_SNAPSHOT_H
#include "kui/vmu_image.h"

enum kui_vmu_save_snapshot_result {
    KUI_VMU_SAVE_SNAPSHOT_OK=0, KUI_VMU_SAVE_SNAPSHOT_ARGUMENT=-1,
    KUI_VMU_SAVE_SNAPSHOT_STATE=-2, KUI_VMU_SAVE_SNAPSHOT_IO=-3,
    KUI_VMU_SAVE_SNAPSHOT_CHANGED=-4, KUI_VMU_SAVE_SNAPSHOT_IMAGE=-5
};

/* A SAVE snapshot, deliberately not a byte-exact physical-card backup.
 * Zero once; never move, modify, reset or reuse after capture starts.
 * All physical metadata, directory/protection bytes and allocated file blocks
 * are preserved exactly. ONLY FAT-free user blocks 0..199 become zero; they
 * are never physically read. No file is selected, filtered or repaired.
 *
 * The source is read-only, serialized, and caller-pinned to one device. The
 * caller must check device identity on EVERY source read. All source callback
 * contexts must remain alive while this view is used, including clone copy.
 * The physical card must not be modified concurrently. Two successful full
 * production filesystem validations are required before this view is ready.
 * Every physical read is byte-compared with an immediate second read; a
 * pinned SHA-256 per block detects later changes. Failures permanently latch.
 * The image/clone APIs retain their existing byte-exact behavior.
 */
struct kui_vmu_save_snapshot {
    const struct kui_vmu_save_snapshot *self;
    struct kui_vmu_image_io source;
    struct kui_vmu_image_workspace image;
    struct kui_vmu_image_summary summary;
    uint8_t block_sha256[256][32];
    uint8_t pinned[256], allocated[200], fat[512];
    _Alignas(32) uint8_t first[512], second[512];
    uint32_t started,active,ready,failed,last_block,source_reads;
    int result,image_result;
};
_Static_assert(sizeof(struct kui_vmu_save_snapshot)<=16384u,
    "Bounded read-only VMU save snapshot workspace");

/* Source descriptor and workspace must not overlap. Source writes/sync must
 * be NULL. On success, summary describes every admitted save and mini-game. */
int kui_vmu_save_snapshot_capture(struct kui_vmu_save_snapshot *,
    const struct kui_vmu_image_io *source,size_t image_bytes);

/* Compatible with kui_vmu_image_io.read_block: context is the captured
 * workspace. Output must not overlap workspace. Failure leaves output intact.
 * A view is { &snapshot, kui_vmu_save_snapshot_read_block, NULL, NULL }. */
int kui_vmu_save_snapshot_read_block(void *context,uint16_t block,void *out);
#endif
