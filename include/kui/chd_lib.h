/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CHD_LIB_H
#define KUI_CHD_LIB_H
#include <stddef.h>
#include <stdint.h>

#define KUI_CHDL_TRACK_MAX 99u
#define KUI_CHDL_FRAME_BYTES 2448u
enum kui_chdl_result {
    KUI_CHDL_OK, KUI_CHDL_INVALID, KUI_CHDL_UNSUPPORTED, KUI_CHDL_RANGE,
    KUI_CHDL_IO, KUI_CHDL_CANCELLED, KUI_CHDL_MEMORY, KUI_CHDL_DECODE
};
struct kui_chdl_io {
    void *context;
    /* Source stays immutable and callbacks/context stay valid until close.
     * Reads are exact; callback returns OK only after filling every byte. */
    enum kui_chdl_result (*read)(void *, uint64_t offset, void *, size_t);
    int (*cancelled)(void *);
};
struct kui_chdl_track {
    uint32_t disc_lba, storage_frame, frames, pad, stored_frames;
    uint16_t sector_bytes; /* 2048 cooked data or 2352 raw data/audio */
    uint8_t control;      /* GDI: 0 audio, 4 data */
    uint8_t mode;         /* 0 audio, 1 MODE1, 2 MODE2 */
};
struct kui_chdl_info {
    uint64_t file_bytes, logical_bytes;
    uint32_t version, hunk_bytes, hunk_count, codecs[4], track_count;
    uint8_t gdrom, legacy_audio_little_endian;
    struct kui_chdl_track tracks[KUI_CHDL_TRACK_MAX];
};
struct kui_chdl;
/* Heap-owned shell-side reader, deliberately not linked into a detached game
 * service. Supports parentless v4/v5 optical CHDs and all upstream CD codecs.
 * Zero pregap/postgap and NONE subchannels are required. Failed open sets
 * *out to NULL. No source file ownership is transferred to this reader. */
enum kui_chdl_result kui_chdl_open(const struct kui_chdl_io *, uint64_t file_bytes,
                                 struct kui_chdl **out);
void kui_chdl_close(struct kui_chdl *);
const struct kui_chdl_info *kui_chdl_get_info(const struct kui_chdl *);
/* Track-relative captured sectors; PAD and four-frame alignment are excluded.
 * Output uses sector_bytes from info and audio is little endian for GDI.
 * Any failure invalidates the hunk cache; earlier sectors may be in output. */
enum kui_chdl_result kui_chdl_read_track(struct kui_chdl *, uint32_t track_index,
    uint32_t first, uint32_t count, void *output, size_t capacity);
/* Canonical CHD storage frames, including subchannel bytes, for verification.
 * CHGT's historical little-endian audio remains as stored in this API. */
enum kui_chdl_result kui_chdl_read_frames(struct kui_chdl *, uint32_t first,
    uint32_t count, void *output, size_t capacity);
const char *kui_chdl_result_name(enum kui_chdl_result);
#endif
