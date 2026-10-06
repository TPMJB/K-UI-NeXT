/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_IMAGE_H
#define KUI_RETAIL_IMAGE_H
#include "kui/game_image.h"
#include "kui/gd_service.h"
#include "kui/storage.h"
#include <stddef.h>
#include <stdint.h>

#define KUI_RETAIL_IMAGE_VERSION 3u
#define KUI_RETAIL_IMAGE_WIRE_BYTES 4096u
/* A map holds up to 99 tracks, GD-ROM's limit, and its storage is a table of
 * 12-byte slots shared by tracks and extents: track i (number i + 1) in slot
 * i, then every track's extents in track order. A disc with many tracks and
 * a card with many fragments both fit, within the readers' unchanged memory
 * (the 16 tracks and 128 extents they used to hold). */
#define KUI_RETAIL_IMAGE_TRACKS 99u
#define KUI_RETAIL_IMAGE_SLOTS 160u
/* Which low resident the stage installs (wire offset 264). The background
 * reader streams SCI microSD from its own interrupt and holds at most
 * KUI_RETAIL_ASYNC_SLOTS slots; validation enforces both. Its EXEC and
 * CHECK top reading up to 20 card blocks each (ASYNC, about twice the
 * standard reader's step) or 25 (ASYNC_EAGER). */
enum kui_retail_reader { KUI_RETAIL_READER_STANDARD, KUI_RETAIL_READER_ASYNC,
    KUI_RETAIL_READER_ASYNC_EAGER };
#define KUI_RETAIL_ASYNC_SLOTS 64u
/* Slots a manifest holds in memory. The background game reader's resident
 * is built with fewer (KUI_RETAIL_ASYNC_SLOTS) to fit its receive areas;
 * the layout is otherwise identical, so a prefix copy of a full manifest
 * using no more slots than that is valid. Such a build has no wire
 * encode/decode/validate. */
#ifndef KUI_RETAIL_MANIFEST_SLOTS
#define KUI_RETAIL_MANIFEST_SLOTS KUI_RETAIL_IMAGE_SLOTS
#endif
#define KUI_RETAIL_IMAGE_MAX_SECTORS 64u
#define KUI_RETAIL_IMAGE_BOOT_MAX (12u * 1024u * 1024u)

/* 2048-byte data and 2336/2352/2448-byte CD sectors. Track extents begin at
 * floor(container_offset/512); the packed offset is the remaining0..511bytes.
 * The final physical block
 * may contain allocation padding, which is never exposed as file data.
 * Extents exactly cover ceil((offset+track_bytes)/512) in file order. Adjacent
 * tracks in a shared file may share one boundary block with disjoint bytes.
 * An audio track may have none: it is listed (TOC, position reports) but its
 * sectors are not mapped, and reads of them are refused. K-UI leaves audio
 * unmapped only when a map would not fit otherwise; no audio is played. */
struct kui_retail_track {
    uint32_t start_lba, end_lba; /* end exclusive */
    uint16_t first_extent;       /* low8: slot; high8: track's byte offset low8 */
    uint8_t extent_count;
    uint8_t control;             /* 4 data, 0 audio; optional cooked flag below. */
};
/* Keep the resident map's 12-byte slots and memory footprint unchanged. The
 * flag is an internal representation only; TOC control must use the helper. */
#define KUI_RETAIL_TRACK_COOKED 0x80u
#define KUI_RETAIL_TRACK_MODE2 0x40u
#define KUI_RETAIL_TRACK_2336 0x20u
#define KUI_RETAIL_TRACK_2448 0x10u
#define KUI_RETAIL_TRACK_OFFSET_HIGH 0x08u
static inline uint32_t kui_retail_track_control(const struct kui_retail_track *t) {
    return t->control & 4u;
}
static inline uint32_t kui_retail_track_first_extent(const struct kui_retail_track *t) {
    return t->first_extent & 255u;
}
static inline uint32_t kui_retail_track_file_offset(const struct kui_retail_track *t) {
    return (t->first_extent >> 8) | ((t->control & KUI_RETAIL_TRACK_OFFSET_HIGH) << 5);
}
static inline uint32_t kui_retail_track_sector_bytes(const struct kui_retail_track *t) {
    return (t->control & KUI_RETAIL_TRACK_COOKED) ? KUI_GAME_DATA_BYTES :
        (t->control & KUI_RETAIL_TRACK_2336) ? 2336u :
        (t->control & KUI_RETAIL_TRACK_2448) ? 2448u : KUI_GAME_RAW_BYTES;
}
static inline uint32_t kui_retail_track_header_bytes(const struct kui_retail_track *t) {
    return (t->control & KUI_RETAIL_TRACK_COOKED) ? 0u :
        (t->control & KUI_RETAIL_TRACK_2336) ? 8u :
        (t->control & KUI_RETAIL_TRACK_MODE2) ? 24u : 16u;
}
#define KUI_RETAIL_IMAGE_CD 1u
#define KUI_RETAIL_IMAGE_SCRAMBLED 2u
#define KUI_RETAIL_IMAGE_BOOT_CRC 4u
struct kui_retail_extent { uint32_t file_block, card_lba, blocks; };
union kui_retail_slot {
    struct kui_retail_track track;
    struct kui_retail_extent extent;
};
_Static_assert(sizeof(union kui_retail_slot) == 12u, "retail map slot size");
_Static_assert(KUI_RETAIL_IMAGE_TRACKS <= KUI_GD_TRACK_MAX, "GD track numbers");
struct kui_retail_manifest {
    uint64_t card_sectors, partition_start, partition_end; /* End exclusive. */
    uint32_t track_count, extent_count, session_lba, boot_lba, boot_bytes;
    uint32_t storage_transport; /* Wire offset28; old zero field means SCIF. */
    uint32_t reader; /* enum kui_retail_reader; wire offset 264, formerly reserved zero */
    uint32_t flags; /* CD session, scrambled boot and source CRC; wire offset268. */
    /* Raw boot sectors retain header/address validation. Cooked boot ranges
     * have no headers: boot_crc32 then identifies the exact full bootfile
     * bytes, including a CE prefix, checked during loading (zero is valid). */
    uint32_t boot_crc32, ip_crc32, gdi_crc32;
    char title[128], product[16], bootfile[24], region[16];
    union kui_retail_slot slots[KUI_RETAIL_MANIFEST_SLOTS]; /* last member */
};
/* Canonical fixed-size LE wire form with magic KUIRTI03 and CRC32 at byte16
 * covering all 4096 bytes with bytes16..19 zeroed: a 320-byte header, then
 * the used slots as 12-byte records from byte 320 (a track's: start, end,
 * packed control byte, extent count byte, offset low8bits, zero byte;
 * first extents follow
 * from the counts). Reserved and unused bytes must be zero. No heap or
 * large automatic objects. Decode clears out on error; encode preserves out
 * on error. Input/output must not alias. */
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
    /* An isolated cache-line-aligned sector permits direct SCI receive DMA
     * without a second resident buffer or touching neighbouring state. */
    _Alignas(32) uint8_t block[512];
};
_Static_assert(offsetof(struct kui_retail_image, block) % 32u == 0,
               "retail sector must begin on its own cache line");
enum kui_game_result kui_retail_image_init(struct kui_retail_image *,
    const struct kui_retail_manifest *, kui_retail_read_block, void *);
enum kui_game_result kui_retail_image_check(const struct kui_retail_manifest *,
    uint32_t lba, uint32_t count, enum kui_game_sector_format);
/* Same request bounds/type checks with a map that was fully validated once
 * and remains immutable. Used by initialized image readers and the resident;
 * externally supplied maps must use the full check/validate APIs above. */
enum kui_game_result kui_retail_image_check_validated(const struct kui_retail_manifest *,
    uint32_t lba, uint32_t count, enum kui_game_sector_format);
/* Preflight range/type/capacity before any IO or output changes. Count <=64.
 * MODE1 checks a 16-byte sync/mode header and copies only 2048 user bytes;
 * Cooked Mode 1 reads copy the stored 2048 bytes directly. RAW copies 2352
 * bytes from raw data or audio tracks and preflights cooked tracks as
 * UNSUPPORTED. Later IO/mode errors
 * may leave partial output. The one-block cache survives read calls so chunks
 * can share a physical block. Init clears it; a failed physical read invalidates
 * it before the callback can supply partial or poisoned bytes. Rejected ranges
 * do not disturb cached data. Manifest and card data must remain immutable
 * and valid until the reader is discarded.
 * Output must not alias the reader or its manifest. */
enum kui_game_result kui_retail_image_read(struct kui_retail_image *,
    uint32_t lba, uint32_t count, enum kui_game_sector_format, void *, size_t);
/* Bytes [skip, skip + bytes) of the user data (2048 bytes per Mode 1 sector,
 * 2352 raw) of the sectors from lba on, with the same checks and caching as
 * kui_retail_image_read; the sectors touched may number at most 64. For
 * transfers that split sectors, such as Windows CE's page-sized DMA stream
 * pieces. bytes must be nonzero; out receives exactly bytes. */
enum kui_game_result kui_retail_image_read_part(struct kui_retail_image *,
    uint32_t lba, uint32_t skip, uint32_t bytes, enum kui_game_sector_format, void *out);

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
