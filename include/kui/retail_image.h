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
    /* boot_crc32 is zero from K-UI: the stage checks each boot sector's
     * header instead of re-reading the file before launch. */
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
 * A resident reader must not depend on filesystem/old-launcher callbacks.
 * The same card and its file bytes must remain unchanged for the active image;
 * hot replacement or modification requires a new validated map and init. */
typedef int (*kui_retail_read_block)(void *, uint32_t, uint8_t[512]);
/* Optional streaming transport, replacing read_block on cache misses while
 * still supplying exactly one block per call. Cache hits call neither one.
 * available includes this block and ends at the current validated request's
 * last payload block or this track extent's end, whichever comes first. It is
 * not permission to prefetch: later calls consume further blocks as needed.
 * A final partial file block may include allocation padding; only declared
 * file bytes are copied to image output. The decoded/validated manifest and
 * card data must remain immutable. The transport bounds each stream, stops
 * before crossing an extent boundary/discontinuity, and reports failures.
 * The caller must close any remaining stream on EVERY image_read return,
 * including mode/range/IO errors, before releasing the physical bus. */
typedef int (*kui_retail_read_run)(void *, uint32_t lba, uint32_t available,
                                  uint8_t[512]);
struct kui_retail_image {
    const struct kui_retail_manifest *manifest;
    kui_retail_read_block read_block;
    kui_retail_read_run read_run; /* Optional; init clears it. read_block required. */
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
 * may leave partial output. The one-block cache survives read calls so chunks
 * can share a physical block. Init clears it; a failed physical read invalidates
 * it before the callback can supply partial or poisoned bytes. Rejected ranges
 * do not disturb cached data. Manifest and card data must remain immutable
 * and valid until the reader is discarded.
 * Output must not alias the reader or its manifest. */
enum kui_game_result kui_retail_image_read(struct kui_retail_image *,
    uint32_t lba, uint32_t count, enum kui_game_sector_format, void *, size_t);

/* Header of one raw Mode 1 sector: the sync pattern, mode 1 and the BCD
 * address of lba (FAD = lba + 150; minutes above 99 carry into the tens
 * nibble, as in recovery_sector.c). The stage checks every boot sector this
 * way, proving the file map pointed at the right sectors; SD CRCs cover the
 * transfer. EDC is not checked: patched executables often leave it stale.
 * Returns KUI_RETAIL_HEADER_OK or the first mismatch. */
enum kui_retail_header {
    KUI_RETAIL_HEADER_OK, KUI_RETAIL_HEADER_SYNC, KUI_RETAIL_HEADER_MODE,
    KUI_RETAIL_HEADER_ADDRESS
};
enum kui_retail_header kui_retail_sector_header(const uint8_t raw[KUI_GAME_RAW_BYTES],
    uint32_t lba);
#endif
