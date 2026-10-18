/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CHD_H
#define KUI_CHD_H
#include <stddef.h>
#include <stdint.h>

#define KUI_CHD_HEADER_BYTES 108u
#define KUI_CHD_FRAME_BYTES 2448u
#define KUI_CHD_HUNK_BYTES (8u * KUI_CHD_FRAME_BYTES)
#define KUI_CHD_TRACK_MAX 99u
#define KUI_CHD_LBA_LIMIT 0x01000000u

enum kui_chd_result {
    KUI_CHD_OK, KUI_CHD_INVALID, KUI_CHD_UNSUPPORTED, KUI_CHD_RANGE,
    KUI_CHD_IO, KUI_CHD_CANCELLED, KUI_CHD_CRC, KUI_CHD_GAP
};
/* This deliberately admits only the project's parentless CHDv4/CHGD export
 * profile, not arbitrary v4 or modern v5 CHDs. Frames are canonical CHD bytes:
 * 2352-byte mainchannel (audio samples big endian), followed by zero subcode.
 * disc_lba and storage_frame are different coordinate systems. PAD denotes
 * omitted sectors and must not be returned as captured disc data. */
struct kui_chd_track {
    uint32_t disc_lba, frames, pad, storage_frame, stored_frames;
    uint8_t mode; /* 0 = audio, 1 = MODE1_RAW, 2 = MODE2_RAW */
};
struct kui_chd_info {
    uint64_t file_bytes, logical_bytes, metadata_offset;
    uint32_t hunk_count, track_count;
    struct kui_chd_track tracks[KUI_CHD_TRACK_MAX];
};
struct kui_chd_io {
    void *context;
    /* Return OK only after reading exactly size bytes. The source is immutable
     * between validated open and all subsequent reads. */
    enum kui_chd_result (*read)(void *, uint64_t offset, void *out, size_t size);
    int (*cancelled)(void *);
};
/* Open validates the entire fixed map and all profile metadata without a
 * complete map allocation or hunk decode. out is unchanged on failure. No io
 * context is retained. Hunk CRCs are checked when hunks are first read. */
enum kui_chd_result kui_chd_open(const struct kui_chd_io *io, uint64_t file_bytes,
                               struct kui_chd_info *out);
/* Allocate this many bytes aligned to workspace_alignment, retain it until
 * reading ends, and initialize before first use or after switching sources.
 * No hidden allocator, capture worker state, or borrowed callback is used.
 * This workspace is NOT admitted to the detached game's memory reservation. */
size_t kui_chd_workspace_bytes(void);
size_t kui_chd_workspace_alignment(void);
enum kui_chd_result kui_chd_workspace_init(void *workspace, size_t bytes);
/* Canonical storage frame slices include synthetic PAD/alignment frames.
 * Exact whole-hunk inflate and CRC validation precede copying each slice.
 * An error invalidates the cache; out may already contain earlier slices. */
enum kui_chd_result kui_chd_read_frames(const struct kui_chd_info *info,
    const struct kui_chd_io *io, void *workspace, size_t workspace_bytes,
    uint32_t first, uint32_t count, void *out, size_t out_bytes);
/* Return captured 2352-byte disc sectors. Synthetic omitted PAD is a GAP.
 * Audio is converted back to the little-endian GDI/CUE sample convention.
 * No CDDA emulation or detached launch is implied by this reader API. */
enum kui_chd_result kui_chd_read_sectors(const struct kui_chd_info *info,
    const struct kui_chd_io *io, void *workspace, size_t workspace_bytes,
    uint32_t lba, uint32_t count, void *out, size_t out_bytes);
const char *kui_chd_result_name(enum kui_chd_result result);
#endif
