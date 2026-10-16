/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_STORE_H
#define KUI_VMU_STORE_H
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_STORE_SECTOR_BYTES 512u
#define KUI_VMU_STORE_BLOCKS 256u
#define KUI_VMU_STORE_IMAGE_BYTES (KUI_VMU_STORE_BLOCKS * KUI_VMU_STORE_SECTOR_BYTES)
#define KUI_VMU_STORE_JOURNAL_SECTORS 2u
#define KUI_VMU_STORE_MAX_EXTENTS 16u

enum kui_vmu_store_result {
    KUI_VMU_STORE_OK = 0,
    KUI_VMU_STORE_ARGUMENT = -1,
    KUI_VMU_STORE_MAPPING = -2,
    KUI_VMU_STORE_IO = -3,
    KUI_VMU_STORE_CORRUPT = -4,
    KUI_VMU_STORE_BLOCKED = -5,
    KUI_VMU_STORE_IDENTITY = -6
};
struct kui_vmu_store_extent { uint64_t first_lba; uint32_t sectors; };
/* Zero means success. One sector per call, absolute card LBA. sync must be a
 * durability AND ordering barrier for every preceding write; writes may tear
 * on power loss, but may not damage other sectors. A driver which cannot give
 * that contract must not enable saves. All callbacks/calls are serialized by
 * the owner, including arbitration with the game reader. */
struct kui_vmu_store_io {
    void *context;
    int (*read_sector)(void *context,uint64_t lba,void *sector);
    int (*write_sector)(void *context,uint64_t lba,const void *sector);
    int (*sync)(void *context);
};
/* Ordered extents describe file sectors in logical file order. Both files
 * must already exist, have exactly these lengths, and be fully allocated and
 * synced. Extents must be exclusively owned, never overlap, and never move
 * while this store is open. Caller validates allocation against filesystem
 * metadata before handing off/unmounting; this layer never writes metadata.
 * card_sectors is the independently obtained device capacity. image_id is a
 * persistent, nonzero 128-bit identity belonging to this image + sidecar pair.
 * Never discard/rebind a sidecar until successful recovery and image sync. */
struct kui_vmu_store_config {
    uint64_t card_sectors;
    uint8_t image_id[16];
    const struct kui_vmu_store_extent *image_extents;
    size_t image_extent_count;
    const struct kui_vmu_store_extent *journal_extents;
    size_t journal_extent_count;
};
/* Caller-owned, no allocation and no 128 KiB RAM mirror. Internal fields are
 * public only to permit static allocation; do not edit or alias I/O buffers
 * with this object. Extent mappings are copied, not borrowed. */
struct kui_vmu_store {
    struct kui_vmu_store_io io;
    struct kui_vmu_store_extent image[KUI_VMU_STORE_MAX_EXTENTS];
    struct kui_vmu_store_extent journal[KUI_VMU_STORE_MAX_EXTENTS];
    size_t image_count,journal_count;
    uint64_t card_sectors,sequence;
    uint32_t mapping_crc;
    uint8_t image_id[16];
    uint8_t data[KUI_VMU_STORE_SECTOR_BYTES];
    uint8_t marker[KUI_VMU_STORE_SECTOR_BYTES];
    unsigned ready;
};
/* open validates the immutable map before any I/O, then replays a complete
 * committed journal block into the ordinary 128 KiB image and syncs it.
 * A CRC-invalid/torn marker is uncommitted and is ignored; a CRC-valid marker
 * with wrong identity/map, invalid fields or corrupt data fails closed.
 * Interrupted recovery is idempotent. Never use/re-export the base image
 * after interruption until open succeeds for its original mapping + sidecar. */
int kui_vmu_store_open(struct kui_vmu_store *store,
    const struct kui_vmu_store_io *io,const struct kui_vmu_store_config *config);
int kui_vmu_store_read(struct kui_vmu_store *store,unsigned block,void *sector);
/* Returns OK only after the redo payload, commit marker, and base image have
 * each passed a durability barrier. On error no success may be acknowledged:
 * a write can be durably committed despite an I/O error; reopen resolves it.
 * Any I/O error blocks this instance until a fresh successful open. The most
 * recent good journal payload is never overwritten before its base is synced.
 * This is atomic per 512-byte VMU block, not atomic across a whole game save. */
int kui_vmu_store_write(struct kui_vmu_store *store,unsigned block,
    const void *sector);
int kui_vmu_store_sync(struct kui_vmu_store *store);
/* For a clean management handoff: first sync the image, then invalidate and
 * sync the sidecar marker. Only after OK and relinquishing this store may the
 * owner relocate/export/edit the base image or recreate its sidecar. Ordinary
 * sync deliberately retains redo protection and does not permit rebinding. */
int kui_vmu_store_checkpoint(struct kui_vmu_store *store);
#endif
