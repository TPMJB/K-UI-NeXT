/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_COLD_IMAGE_H
#define KUI_COLD_IMAGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_COLD_IMAGE_MAGIC0 UINT32_C(0x4349554b)
#define KUI_COLD_IMAGE_MAGIC1 UINT32_C(0x31304d49)
#define KUI_COLD_IMAGE_SEED_BYTES 32u
#define KUI_COLD_IMAGE_CAP_OFFSET 0x1c0u
/* Immutable entry-prefix capability, separate from the mutable stage seed.
 * Old child images have no capability and never pay for preserving an image. */
bool kui_cold_image_capable(const void *prefix,size_t bytes,uint32_t image_bytes);
/* Temporary cold-transition scratch; never a worker/game reservation. Its
 * owner must be retired, all DMA quiet, and both P1 aliases published before
 * an uncached writer preserves a source here. No address from a guest is used. */
#define KUI_COLD_IMAGE_SCRATCH UINT32_C(0x8c400000)
#define KUI_COLD_IMAGE_SCRATCH_END UINT32_C(0x8cc00000)
struct kui_cold_image_seed {
    uint32_t magic0,magic1,version,bytes,source,image_bytes,image_crc32,crc32;
};
_Static_assert(sizeof(struct kui_cold_image_seed)==KUI_COLD_IMAGE_SEED_BYTES,"Cold image seed ABI");
bool kui_cold_image_seed_blank(const struct kui_cold_image_seed *);
bool kui_cold_image_seed_valid(const struct kui_cold_image_seed *);
size_t kui_cold_image_seed_find(const void *,size_t);
bool kui_cold_image_seed_configure(struct kui_cold_image_seed *,uint32_t source,
    uint32_t bytes,uint32_t crc32);
/* A descriptor is only a hint. Recheck all preserved bytes and compute a fresh
 * full SHA for the independent owner predicate before the first output write.
 * This pure operation runs in the fresh cached high stage, not a live SDK or
 * storage callback. Failure leaves the destination unchanged; normal mapped
 * source reading remains available. No persisted hash is accepted as fresh. */
bool kui_cold_image_restore(const struct kui_cold_image_seed *,void *out,size_t capacity,
    const void *source,size_t source_capacity,uint32_t expected_crc32,
    bool (*owner)(uint32_t,const uint8_t [32]));
#endif
