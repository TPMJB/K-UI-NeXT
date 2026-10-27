/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_IMAGE_SUBSET_H
#define KUI_VMU_IMAGE_SUBSET_H
#include "kui/vmu_image.h"

enum kui_vmu_image_subset_result {
    KUI_VMU_IMAGE_SUBSET_SELECTION = -7
};

/* Pure, synchronous construction of a separate standard 200-user-block card.
 * Source and destination are exactly 131072-byte, caller-owned memory spans;
 * no files, devices, journals, identities or live-card bindings are accessed.
 * The caller keeps source and slots immutable for the entire call.
 *
 * slots explicitly identifies 1..208 occupied directory entries, without
 * duplicates. Slot 0 is block 253, byte 0; slot 16 is block 252, byte 0;
 * slot 207 is block 241, byte 480. No names, prefixes, package headers or
 * game-specific rules infer additional files. The caller must choose every
 * related entry it wants retained, including system files and mini-games.
 *
 * The entire source filesystem is admitted before destination is touched.
 * On success, selected entries keep their original 32 bytes and locations,
 * complete 512-byte allocated blocks, FAT links and original block numbers.
 * The entire root and the system FAT entries remain byte-identical. Removed
 * directory entries, every unused data block (including 200..240), and all
 * newly free user blocks are zeroed; their user FAT entries become FREE.
 * Opaque package bytes, protection flags and header offsets are never edited.
 * The completed image is independently revalidated before success.
 *
 * Destination, workspace and summary must each be disjoint from one another
 * and from source and slots. Source and slots may overlap one another because
 * both are read-only. Invalid arguments/overlap return IMAGE_ARGUMENT without
 * modifying any supplied buffer. Admission/selection failures leave source
 * and destination unchanged and clear summary. A VERIFY failure means discard
 * destination. Other return codes come from kui_vmu_image_validate; a
 * duplicate, inactive or out-of-range chosen slot returns SUBSET_SELECTION.
 * A zero slot_count is an invalid argument.
 *
 * A successful buffer is only an offline image, not a loader-installable pair.
 * Any subsequent installation needs its own new identity, zero journal and
 * ownership/profile admission; this helper does not create or change them.
 */
int kui_vmu_image_subset(const void *source, size_t source_bytes,
    void *destination, size_t destination_bytes,
    const uint16_t *slots, size_t slot_count,
    struct kui_vmu_image_workspace *workspace,
    struct kui_vmu_image_summary *summary);

#endif
