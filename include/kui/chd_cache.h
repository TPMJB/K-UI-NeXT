/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CHD_CACHE_H
#define KUI_CHD_CACHE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_CHD_CACHE_TRACK_MAX 99u
#define KUI_CHD_CACHE_NAME_BYTES 12u /* track99.bin and NUL */
#define KUI_CHD_CACHE_GDI_BYTES 8192u
#define KUI_CHD_CACHE_CHUNK_SECTORS 16u
#define KUI_CHD_CACHE_ID_BYTES 32u

enum kui_chd_cache_result {
    KUI_CHD_CACHE_OK, KUI_CHD_CACHE_INVALID, KUI_CHD_CACHE_RANGE,
    KUI_CHD_CACHE_IO, KUI_CHD_CACHE_CANCELLED, KUI_CHD_CACHE_VERIFY
};
/* Exported frames only: omit CHD PAD/alignment frames and virtual pregaps.
 * read_track must return little-endian audio samples for GDI. Raw data keeps
 * all 2352 main-channel bytes; cooked data supplies 2048 user bytes. */
struct kui_chd_cache_track {
    uint32_t disc_lba, frames;
    uint16_t sector_bytes; /* 2048 data, or 2352 data/audio. */
    uint8_t control;       /* 4 data, 0 audio. */
};
struct kui_chd_cache_plan {
    unsigned count;
    struct kui_chd_cache_track tracks[KUI_CHD_CACHE_TRACK_MAX];
};
struct kui_chd_cache_report {
    uint64_t payload_bytes, descriptor_bytes;
    unsigned count;
    uint8_t source_id[KUI_CHD_CACHE_ID_BYTES];
    uint8_t track_sha256[KUI_CHD_CACHE_TRACK_MAX][32];
    uint8_t descriptor_sha256[32];
};
struct kui_chd_cache_io {
    void *ctx;
    /* Source and existing caches are read-only. The caller prepares a fresh,
     * exclusively owned staging directory before calling this function.
     * Every read/write callback returns true only after exact completion. */
    bool (*read_track)(void *, unsigned track, uint32_t first, uint32_t count,
                       void *out, size_t bytes);
    bool (*create_track)(void *, unsigned track, const char *name, uint64_t bytes);
    bool (*write_track)(void *, unsigned track, uint64_t offset, const void *, size_t);
    bool (*read_output)(void *, unsigned track, uint64_t offset, void *, size_t);
    bool (*sync_track)(void *, unsigned track);
    /* Close and confirm the exact length. Called on the current file during
     * error cleanup too; callbacks must not turn incomplete files into finals. */
    bool (*finish_track)(void *, unsigned track, uint64_t expected_bytes);
    /* Called only after all tracks pass complete SHA256 readback, close, and
     * cancellation checks. Write/sync the descriptor and identity manifest in
     * staging, close them, then atomically rename the NEW directory. Never
     * replace an existing destination. Return true only after publication. */
    bool (*publish)(void *, const char *gdi, size_t bytes,
                    const struct kui_chd_cache_report *);
    /* Clean up only this call's uncommitted staging files. Must tolerate any
     * create/write/close failure and never remove source or old cache files. */
    void (*abort)(void *);
    bool (*cancelled)(void *);
    void (*progress)(void *, uint64_t done, uint64_t total, bool verify);
};
/* No hidden allocation or retained callback. Workspace must be aligned as
 * returned here and may be released immediately after export returns. */
size_t kui_chd_cache_workspace_bytes(void);
size_t kui_chd_cache_workspace_alignment(void);
/* Includes output payload plus exact GDI bytes, not filesystem cluster or
 * directory/manifest overhead. Adapter must reserve those separately. */
enum kui_chd_cache_result kui_chd_cache_bound(const struct kui_chd_cache_plan *, uint64_t *bytes);
enum kui_chd_cache_result kui_chd_cache_export(const struct kui_chd_cache_plan *,
    const uint8_t source_id[KUI_CHD_CACHE_ID_BYTES], const struct kui_chd_cache_io *,
    void *workspace, size_t workspace_bytes, struct kui_chd_cache_report *);
const char *kui_chd_cache_result_name(enum kui_chd_cache_result);
#endif
