/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_IMAGE_H
#define KUI_VMU_IMAGE_H
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_IMAGE_BLOCK_BYTES 512u
#define KUI_VMU_IMAGE_BLOCKS 256u
#define KUI_VMU_IMAGE_BYTES 131072u
#define KUI_VMU_IMAGE_USER_BLOCKS 200u
#define KUI_VMU_IMAGE_DIRECTORY_ENTRIES 208u

enum kui_vmu_image_result {
    KUI_VMU_IMAGE_OK = 0,
    KUI_VMU_IMAGE_ARGUMENT = -1,
    KUI_VMU_IMAGE_IO = -2,
    KUI_VMU_IMAGE_NOT_BLANK = -3,
    KUI_VMU_IMAGE_GEOMETRY = -4,
    KUI_VMU_IMAGE_FILESYSTEM = -5,
    KUI_VMU_IMAGE_VERIFY = -6
};

/* Logical image-block callbacks, not card LBA callbacks. Exact length of the
 * containing image must independently be obtained from its filesystem. Zero
 * means success. sync is a durable ordering barrier. No callback retains data
 * pointers; access is serialized, and the image cannot change concurrently. */
struct kui_vmu_image_io {
    void *context;
    int (*read_block)(void *context, uint16_t block, void *data);
    int (*write_block)(void *context, uint16_t block, const void *data);
    int (*sync)(void *context);
};

/* Caller-owned preparation scratch, about 4 KiB, never a 128 KiB card mirror.
 * Do not alias it with callback buffers, the image, or the result. */
struct kui_vmu_image_workspace {
    uint8_t block[512];
    uint8_t fat[512];
    uint8_t used[200];
    uint8_t names[208][12];
};

struct kui_vmu_image_summary {
    unsigned files;
    unsigned game_files;
    unsigned used_blocks;
    unsigned free_blocks;
};

/* Pure synthesis of one canonical blank-card block, using the same default
 * timestamp and bytes as format_blank(NULL timestamp). No image, I/O or
 * workspace is needed. Invalid block/output returns ARGUMENT without writing. */
int kui_vmu_image_blank_block(uint16_t block, uint8_t out[512]);

/* Creates a standard formatted image only if EVERY block is entirely zero.
 * A nonzero byte causes NOT_BLANK before any write or sync. Never formats an
 * imported/existing card automatically. Timestamp is 8-byte KOS BCD order
 * century,year,month,day,hour,minute,second,weekday(Monday=0). NULL chooses
 * 2000-01-01 00:00:00 (Saturday). Writes and verifies all 256 blocks, installing
 * the valid root last after a durability barrier. Image length must be exact.
 * On I/O failure, discard/recreate a newly prepared image; no success implied.
 */
int kui_vmu_image_format_blank(const struct kui_vmu_image_io *io,
    size_t image_bytes, const uint8_t timestamp[8],
    struct kui_vmu_image_workspace *workspace);

/* Read-only admission of standard 200-user-block geometry. Reads all blocks,
 * checks system allocation, each entry's type, size, header offset and complete
 * file chain, and rejects cycles/shared blocks/orphans and unsupported layouts.
 * Names, copy-protect bytes, timestamps, entry bytes 0x1c-0x1f, the root's
 * color and icon are what games wrote, never refused. Free blocks may hold
 * stale data. This validates the VMU filesystem, not each game's opaque save
 * payload/CRC. Summary is cleared on failure. names[] is no longer used. */
int kui_vmu_image_validate(const struct kui_vmu_image_io *io,
    size_t image_bytes, struct kui_vmu_image_workspace *workspace,
    struct kui_vmu_image_summary *summary);

#endif
