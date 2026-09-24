/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_LOADER_PROBE_H
#define KUI_LOADER_PROBE_H

#include <stddef.h>
#include <stdint.h>

/* G3 deliberately uses an original, tiny fixture. This is a project ABI, not
 * the retail Dreamcast GD BIOS ABI. Every on-card integer is little endian. */
#define KUI_LOADER_PROBE_VERSION 1u
#define KUI_LOADER_PROBE_PATH "/KUI/apps/games/probe.dat"
#define KUI_LOADER_PROBE_EXTENTS 128u
#define KUI_LOADER_PROBE_MANIFEST_BYTES 1600u
#define KUI_LOADER_PROBE_BLOCK_BYTES 512u
#define KUI_LOADER_PROBE_BLOCKS 111u
#define KUI_LOADER_PROBE_FILE_BYTES 56832u
#define KUI_LOADER_PROBE_RAW_BYTES 2352u
#define KUI_LOADER_PROBE_DATA_BYTES 2048u
#define KUI_LOADER_PROBE_TRACKS 3u
#define KUI_LOADER_PROBE_MAX_SECTORS 4u
#define KUI_LOADER_PROBE_STATUS_BYTES 16u
#define KUI_LOADER_PROBE_TOC_BYTES 48u

enum kui_loader_probe_result {
    KUI_LP_OK = 0, KUI_LP_PENDING, KUI_LP_BUSY, KUI_LP_INVALID,
    KUI_LP_CHECKSUM, KUI_LP_RANGE, KUI_LP_GAP, KUI_LP_AUDIO,
    KUI_LP_UNSUPPORTED, KUI_LP_IO, KUI_LP_CANCELLED, KUI_LP_FORMAT
};
enum kui_loader_probe_opcode {
    KUI_LP_STATUS = 1, KUI_LP_TOC = 2, KUI_LP_READ = 3
};
enum kui_loader_probe_format { KUI_LP_RAW = 0, KUI_LP_MODE1 = 1 };

struct kui_loader_probe_extent { uint32_t file_block, card_lba, blocks; };
struct kui_loader_probe_manifest {
    uint32_t card_sectors, extent_count;
    struct kui_loader_probe_extent extents[KUI_LOADER_PROBE_EXTENTS];
};
struct kui_loader_probe_request { uint32_t opcode, lba, count, format; };

/* STATUS produces four LE u32s: version, media-present=1, tracks=3, max-count=4.
 * TOC produces three records of four LE u32s: number, control, start, end
 * (exclusive). Submit only queues a request; the first poll performs the
 * bounded read. No backend work occurs on rejected or cancelled requests.
 * Buffers belong to the client until completion/cancellation. Discard their
 * contents after any error. Only one request may be pending at a time. */
struct kui_loader_probe_api {
    uint32_t version, bytes;
    enum kui_loader_probe_result (*submit)(
        const struct kui_loader_probe_request *, void *, uint32_t, uint32_t *);
    enum kui_loader_probe_result (*poll)(uint32_t, uint32_t *);
    enum kui_loader_probe_result (*cancel)(uint32_t);
    /* Resident-owned display only. The client has no KOS/runtime references. */
    void (*report)(const char *, uint32_t passed, uint32_t detail);
    uint32_t (*read_count)(void);
};

/* Return zero only after exactly one physical card block has been supplied. */
typedef int (*kui_loader_probe_read_block)(void *, uint32_t, uint8_t[512]);
struct kui_loader_probe_service {
    struct kui_loader_probe_manifest manifest;
    kui_loader_probe_read_block read;
    void *read_context;
    struct kui_loader_probe_request request;
    uint8_t *output;
    uint32_t capacity, token, completed_bytes;
    enum kui_loader_probe_result completion;
    uint32_t pending, cached_block, cache_valid, blocks_read;
    uint8_t block[512], raw[2352];
};

enum kui_loader_probe_result kui_loader_probe_manifest_encode(
    const struct kui_loader_probe_manifest *, uint8_t out[1600]);
enum kui_loader_probe_result kui_loader_probe_manifest_decode(
    const uint8_t wire[1600], struct kui_loader_probe_manifest *);
enum kui_loader_probe_result kui_loader_probe_init(
    struct kui_loader_probe_service *, const struct kui_loader_probe_manifest *,
    kui_loader_probe_read_block, void *context);
enum kui_loader_probe_result kui_loader_probe_submit(
    struct kui_loader_probe_service *, const struct kui_loader_probe_request *,
    void *output, uint32_t capacity, uint32_t *token);
enum kui_loader_probe_result kui_loader_probe_poll(
    struct kui_loader_probe_service *, uint32_t token, uint32_t *bytes);
enum kui_loader_probe_result kui_loader_probe_cancel(
    struct kui_loader_probe_service *, uint32_t token);
uint8_t kui_loader_probe_fixture_byte(uint32_t file_byte);
const char *kui_loader_probe_result_name(enum kui_loader_probe_result);

/* Standalone client entry: invoked in separate low RAM after the old runtime
 * has been erased. It uses only the resident export table and its own code. */
uint32_t kui_probe_client_main(const struct kui_loader_probe_api *);

#endif
