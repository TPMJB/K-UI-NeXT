/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAME_IMAGE_H
#define KUI_GAME_IMAGE_H

#include <stddef.h>
#include <stdint.h>

#define KUI_GAME_TRACK_MAX 99u
#define KUI_GAME_NAME_CAP 128u
#define KUI_GAME_GDI_LIMIT 32768u
#define KUI_GAME_RAW_BYTES 2352u
#define KUI_GAME_DATA_BYTES 2048u
/* Same upper FAD as the existing raw-sector validation; exclusive LBA end. */
#define KUI_GAME_LBA_LIMIT (719999u - 150u + 1u)

enum kui_game_result {
    KUI_GAME_OK, KUI_GAME_INVALID, KUI_GAME_SYNTAX, KUI_GAME_UNSUPPORTED,
    KUI_GAME_NOT_FOUND, KUI_GAME_IO, KUI_GAME_CANCELLED, KUI_GAME_FILE_SIZE,
    KUI_GAME_OVERLAP, KUI_GAME_RANGE, KUI_GAME_GAP, KUI_GAME_AUDIO,
    KUI_GAME_MODE
};

/* Names are validated single components, relative to the selected GDI folder.
 * All callbacks are read-only and synchronous. read must return OK only after
 * supplying exactly size bytes; short reads are IO. The adapter checks its
 * cancellation source and may return CANCELLED from either callback. It owns
 * ctx and underlying files for the lifetime of the image. No callback is kept
 * alive across a return to the caller. */
struct kui_game_file_ops {
    void *ctx;
    enum kui_game_result (*stat)(void *ctx, const char *name, uint64_t *bytes);
    enum kui_game_result (*read)(void *ctx, const char *name, uint64_t offset,
                                 void *out, size_t size);
};

struct kui_game_image_track {
    uint32_t start_lba, end_lba; /* End exclusive, derived from exact file size. */
    unsigned number, control; /* 0 = audio, 4 = data. */
    uint64_t file_bytes;
    char name[KUI_GAME_NAME_CAP];
};

struct kui_game_image {
    struct kui_game_file_ops files;
    unsigned count;
    struct kui_game_image_track tracks[KUI_GAME_TRACK_MAX];
};

enum kui_game_sector_format { KUI_GAME_SECTOR_RAW, KUI_GAME_SECTOR_MODE1 };

/* Accept only consecutive track numbers, raw 2352-byte files and zero offsets.
 * Validate all file sizes/overlaps before publishing out; out is unchanged on
 * failure. GDI is a bounded byte span and need not be NUL-terminated. */
enum kui_game_result kui_game_image_open(const void *gdi, size_t size,
    const struct kui_game_file_ops *files, struct kui_game_image *out);

/* Check the entire LBA request without reading data. Explicit gaps (including
 * the low/high-density session gap) are GAP, not implicit zero-fill. Requests
 * outside the image's first/final extent are RANGE. */
enum kui_game_result kui_game_image_check(const struct kui_game_image *image,
    uint32_t lba, uint32_t count, enum kui_game_sector_format format);

/* RAW supports data and audio. MODE1 validates sync/mode and extracts 2048 user
 * bytes at offset 16; Mode 2 and audio are unsupported for this first stage.
 * Preflights the complete range, track types and output size before any read or
 * output modification. Later IO/cancel/mode errors may leave a partial output;
 * callers must discard all output unless OK. No whole-image hash is performed. */
enum kui_game_result kui_game_image_read(const struct kui_game_image *image,
    uint32_t lba, uint32_t count, enum kui_game_sector_format format,
    void *out, size_t size);

const char *kui_game_result_name(enum kui_game_result result);

#endif
