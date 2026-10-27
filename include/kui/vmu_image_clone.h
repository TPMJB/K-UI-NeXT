/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_IMAGE_CLONE_H
#define KUI_VMU_IMAGE_CLONE_H
#include "kui/vmu_image.h"

enum kui_vmu_image_clone_result {
    KUI_VMU_CLONE_OK=0,KUI_VMU_CLONE_ARGUMENT=-1,KUI_VMU_CLONE_STATE=-2,
    KUI_VMU_CLONE_IO=-3,KUI_VMU_CLONE_CHANGED=-4,KUI_VMU_CLONE_IMAGE=-5,
    KUI_VMU_CLONE_NOT_ZERO=-6,KUI_VMU_CLONE_VERIFY=-7
};
enum kui_vmu_image_clone_phase {
    KUI_VMU_CLONE_CAPTURE=1,KUI_VMU_CLONE_VALIDATE=2,KUI_VMU_CLONE_READY=3,
    KUI_VMU_CLONE_RECHECK=4,KUI_VMU_CLONE_ZERO=5,KUI_VMU_CLONE_BODY=6,
    KUI_VMU_CLONE_ROOT=7,KUI_VMU_CLONE_READBACK=8,KUI_VMU_CLONE_DONE=9
};
/* Zero once; never move, edit, reset or reuse after capture starts. The source
 * descriptor is copied and may only read: write_block and sync MUST be NULL.
 * The caller pins the physical device/slot and checks identity on every read.
 * No physical card or filesystem API is used by this helper.
 *
 * After captured==1, summary, sha256, block_crc and image.names[][12] describe
 * the validated snapshot. Names are 12-byte normalized keys, not C strings.
 * They are readonly preview data; the remaining fields are private work.
 * Every failure latches. A valid snapshot can be rechecked before filesystem
 * allocation, but copy is attempted once and completed only after readback.
 */
struct kui_vmu_image_clone {
    const struct kui_vmu_image_clone *self;
    struct kui_vmu_image_io source;
    struct kui_vmu_image_workspace image;
    struct kui_vmu_image_summary summary;
    uint32_t block_crc[256];
    uint8_t sha256[32];
    _Alignas(32) uint8_t buffer[512];
    uint32_t started,active,captured,copy_started,completed,failed,phase,last_block;
    uint32_t source_reads,destination_reads,destination_writes,syncs;
    int result,image_result;
};
_Static_assert(sizeof(struct kui_vmu_image_clone)<=8192u,"Bounded VMU clone workspace");
/* All callback contexts remain alive through copy; argument structures must
 * be disjoint from the workspace. Source length is exactly 131072 bytes.
 * Capture pins all sectors, then validates through CRC-checked source reads.
 */
int kui_vmu_image_clone_capture(struct kui_vmu_image_clone *,
    const struct kui_vmu_image_io *source,size_t image_bytes);
/* Fresh complete source CRC/SHA comparison. No destination I/O. */
int kui_vmu_image_clone_verify_source(struct kui_vmu_image_clone *);
/* Caller MUST supply an exclusively owned CREATE_NEW 131072-byte destination.
 * All 256 sectors must be zero before any write. Copies bytes unchanged,
 * including protection flags, padding and unused blocks. A fresh source SHA
 * must match before body sync -> root255 write -> sync. Then every sector and
 * the VMU filesystem are verified. No destination pointer survives this call.
 * Caller publishes a new pair identity only after success, with a verified
 * zero journal; a failed/partial pair must never be reused or exported.
 */
int kui_vmu_image_clone_copy(struct kui_vmu_image_clone *,
    const struct kui_vmu_image_io *destination,size_t image_bytes);
#endif
