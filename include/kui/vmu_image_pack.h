/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_IMAGE_PACK_H
#define KUI_VMU_IMAGE_PACK_H
#include "kui/vmu_image.h"

/* One already extracted file, in logical file-block order. The directory entry
 * and opaque payload remain byte-exact except for the relocated first block. */
struct kui_vmu_image_file {
    uint8_t entry[32];
    const void *payload;
    size_t bytes;
};

/* Builds a NEW standard 128 KiB image from 1..200 files occupying at most 200
 * blocks. Accepts 0x33 data files and at most one 0xcc minigame. The minigame is
 * placed contiguously from block zero; data files use descending free blocks.
 * Directory order follows the input order. All inactive entries and unused
 * blocks are zero, with canonical blank-card system metadata.
 *
 * Entries must satisfy production admission rules for type, nonzero length
 * and header offset; copy-protect bytes, names and bytes 0x1c-0x1f are copied
 * as written, as admission accepts them. Payload length must equal entry
 * block count times 512. Original first blocks are not interpreted: only
 * entry bytes 2..3 are replaced. Payloads, padding and internal checksums
 * are copied without alteration.
 *
 * All inputs are read-only and may overlap each other. destination, workspace
 * and summary must be mutually disjoint and must not overlap the descriptor
 * array or any payload. Invalid arguments/aliasing return ARGUMENT; malformed
 * entries or capacity violations return FILESYSTEM. Complete prevalidation
 * leaves all three output buffers unchanged on those failures. On success the
 * completed image is admitted by kui_vmu_image_validate; a subsequent VERIFY
 * failure may leave the constructed image and workspace modified. The caller
 * must keep inputs stable throughout the call. No heap allocation or I/O.
 */
int kui_vmu_image_pack(const struct kui_vmu_image_file *files, size_t count,
    void *destination, size_t bytes,
    struct kui_vmu_image_workspace *workspace,
    struct kui_vmu_image_summary *summary);

#endif
