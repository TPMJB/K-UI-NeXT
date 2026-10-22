/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RESIDENT_IMAGE_H
#define KUI_RESIDENT_IMAGE_H
#include "kui/game_image.h"
#include <stddef.h>
#include <stdint.h>

#define KUI_RESIDENT_IMAGE_VERSION 1u
#define KUI_RESIDENT_IMAGE_WIRE_BYTES 65536u
#define KUI_RESIDENT_IMAGE_EXTENTS 4096u
#define KUI_RESIDENT_IMAGE_SAMPLES 16u
#define KUI_RESIDENT_IMAGE_MAX_SECTORS 64u
#define KUI_RESIDENT_IMAGE_TITLE_BYTES 128u

/* Consecutive raw-2352 tracks, with file offset zero. File length is exactly
 * (end_lba-start_lba)*2352. The final 512-byte allocation may contain padding;
 * that padding is never exposed as file data. Extents cover ceil(file/512)
 * blocks, in file order, with no holes, aliasing, or overlaps anywhere. */
struct kui_resident_track {
    uint32_t number, start_lba, end_lba, control, first_extent, extent_count;
};
struct kui_resident_extent { uint32_t file_block, card_lba, blocks; };
struct kui_resident_sample { uint32_t lba, count, format, crc32; };
struct kui_resident_manifest {
    uint64_t card_sectors, partition_start, partition_end; /* End exclusive. */
    uint32_t track_count, extent_count, sample_count;
    uint32_t session_lba, boot_lba, boot_bytes, gdi_crc32;
    char title[128], product[16], bootfile[24], region[16];
    struct kui_resident_track tracks[KUI_GAME_TRACK_MAX];
    struct kui_resident_extent extents[KUI_RESIDENT_IMAGE_EXTENTS];
    struct kui_resident_sample samples[KUI_RESIDENT_IMAGE_SAMPLES];
};
/* Fixed-size, pointer-free, canonical LE wire form. CRC32 at byte16 covers the
 * entire wire with bytes16..19 zeroed. Every unused/reserved byte must be zero.
 * No heap and no large automatic temporary object is used. Decode clears out
 * on error; encode leaves out unchanged on error. Input/output must not alias.
 * A decoded manifest is immutable for the lifetime of a reader. */
enum kui_game_result kui_resident_manifest_validate(
    const struct kui_resident_manifest *);
enum kui_game_result kui_resident_manifest_encode(
    const struct kui_resident_manifest *, uint8_t out[KUI_RESIDENT_IMAGE_WIRE_BYTES]);
enum kui_game_result kui_resident_manifest_decode(
    const uint8_t wire[KUI_RESIDENT_IMAGE_WIRE_BYTES], struct kui_resident_manifest *);
/* IEEE CRC32, same chaining convention as zlib.crc32. */
uint32_t kui_resident_crc32(uint32_t previous, const void *, size_t);

/* Return zero only after supplying exactly one physical 512-byte card block.
 * No filesystem or old-launcher callbacks may be used by a resident caller. */
typedef int (*kui_resident_read_block)(void *, uint32_t, uint8_t[512]);
struct kui_resident_image {
    const struct kui_resident_manifest *manifest;
    kui_resident_read_block read_block;
    void *context;
    uint32_t blocks_read, cached_lba, cache_valid;
    uint8_t block[512], raw[KUI_GAME_RAW_BYTES];
};
enum kui_game_result kui_resident_image_init(struct kui_resident_image *,
    const struct kui_resident_manifest *, kui_resident_read_block, void *);
/* Preflight all ranges/types before any physical reads or output changes.
 * Requests are bounded to 64 sectors. Runtime IO/sector-format failures can
 * leave partial output: discard it unless OK. Cache is invalidated per read,
 * so repeated reads really reach SD. Manifest storage remains caller-owned. */
enum kui_game_result kui_resident_image_check(
    const struct kui_resident_manifest *, uint32_t lba, uint32_t count,
    enum kui_game_sector_format);
enum kui_game_result kui_resident_image_read(struct kui_resident_image *,
    uint32_t lba, uint32_t count, enum kui_game_sector_format, void *, size_t);
#endif
