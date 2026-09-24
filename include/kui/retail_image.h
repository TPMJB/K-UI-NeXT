/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_IMAGE_H
#define KUI_RETAIL_IMAGE_H
#include "kui/game_image.h"
#include <stddef.h>
#include <stdint.h>

#define KUI_RETAIL_IMAGE_VERSION 1u
#define KUI_RETAIL_IMAGE_WIRE_BYTES 4096u
#define KUI_RETAIL_IMAGE_TRACKS 16u
#define KUI_RETAIL_IMAGE_EXTENTS 128u
#define KUI_RETAIL_IMAGE_MAX_SECTORS 64u
#define KUI_RETAIL_IMAGE_BOOT_MAX (12u * 1024u * 1024u)

/* Raw 2352-byte track files with zero file offsets. The final physical block
 * may contain allocation padding, which is never exposed as file data.
 * Extents exactly cover ceil(file_bytes/512), in file order, without aliases. */
struct kui_retail_track {
    uint32_t number, start_lba, end_lba, control, first_extent, extent_count;
};
struct kui_retail_extent { uint32_t file_block, card_lba, blocks; };
struct kui_retail_manifest {
    uint64_t card_sectors, partition_start, partition_end; /* End exclusive. */
    uint32_t track_count, extent_count, session_lba, boot_lba, boot_bytes;
    uint32_t boot_crc32, ip_crc32, gdi_crc32;
    char title[128], product[16], bootfile[24], region[16];
    struct kui_retail_track tracks[KUI_RETAIL_IMAGE_TRACKS];
    struct kui_retail_extent extents[KUI_RETAIL_IMAGE_EXTENTS];
};
/* Canonical fixed-size LE wire form with magic KUIRTI01 and CRC32 at byte16
 * covering all 4096 bytes with bytes16..19 zeroed. Reserved and unused bytes
 * must be zero. No heap or large automatic objects. Decode clears out on
 * error; encode preserves out on error. Input/output must not alias. */
enum kui_game_result kui_retail_manifest_validate(const struct kui_retail_manifest *);
enum kui_game_result kui_retail_manifest_encode(const struct kui_retail_manifest *,
    uint8_t out[KUI_RETAIL_IMAGE_WIRE_BYTES]);
enum kui_game_result kui_retail_manifest_decode(
    const uint8_t wire[KUI_RETAIL_IMAGE_WIRE_BYTES], struct kui_retail_manifest *);
uint32_t kui_retail_crc32(uint32_t previous, const void *, size_t);

/* Supplies exactly one physical 512-byte SD block, returning zero on success.
 * A resident reader must not depend on filesystem/old-launcher callbacks. */
typedef int (*kui_retail_read_block)(void *, uint32_t, uint8_t[512]);
struct kui_retail_image {
    const struct kui_retail_manifest *manifest;
    kui_retail_read_block read_block;
    void *context;
    uint32_t blocks_read, cached_lba, cache_valid;
    uint8_t block[512];
};
enum kui_game_result kui_retail_image_init(struct kui_retail_image *,
    const struct kui_retail_manifest *, kui_retail_read_block, void *);
enum kui_game_result kui_retail_image_check(const struct kui_retail_manifest *,
    uint32_t lba, uint32_t count, enum kui_game_sector_format);
/* Preflight range/type/capacity before any IO or output changes. Count <=64.
 * MODE1 checks a 16-byte sync/mode header and copies only 2048 user bytes;
 * RAW copies 2352 bytes from either data or audio tracks. Later IO/mode errors
 * may leave partial output. Cache is invalidated for every read command.
 * Manifest must remain immutable and valid until the reader is discarded.
 * Output must not alias the reader or its manifest. */
enum kui_game_result kui_retail_image_read(struct kui_retail_image *,
    uint32_t lba, uint32_t count, enum kui_game_sector_format, void *, size_t);
#endif
