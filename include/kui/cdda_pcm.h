/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_PCM_H
#define KUI_CDDA_PCM_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_CDDA_PCM_SECTOR_BYTES 2352u
#define KUI_CDDA_PCM_FRAMES_PER_SECTOR 588u
#define KUI_CDDA_PCM_CACHE_BYTES 512u

enum kui_cdda_pcm_status {
    KUI_CDDA_PCM_OK = 0,
    KUI_CDDA_PCM_EOF,
    KUI_CDDA_PCM_INVALID,
    KUI_CDDA_PCM_IO
};

/* An exact, bounded file read: false means the requested bytes are unavailable.
 * The destination is 32-byte aligned. Reads are at most 512 bytes, with the
 * final read clipped to the actual file length. */
typedef bool (*kui_cdda_pcm_read_fn)(void *ctx, uint32_t offset,
    uint8_t *out, size_t bytes);

struct kui_cdda_pcm_source {
    uint32_t file_bytes;      /* Actual backing file length, including any prefix. */
    uint32_t backing_offset;  /* First byte of the track's first complete sector. */
    uint32_t sector_stride;   /* 2352 PCM bytes, or 2352 PCM + 96 subchannel bytes. */
    uint32_t sectors;         /* Complete sectors belonging to this audio track. */
    bool big_endian;          /* Explicit PCM byte order; no heuristic detection. */
};

/* No heap, codec, audio hardware, or floating point. The public position counts
 * decoded stereo frames only; cache prefetch never advances it. Read position
 * for reporting and use seek to change it; all fields are otherwise private.
 * Stack/static declarations get the required 32-byte alignment. A dynamically
 * allocated instance also requires an allocator supplying 32-byte alignment. */
struct kui_cdda_pcm {
    struct kui_cdda_pcm_source source;
    kui_cdda_pcm_read_fn read;
    void *ctx;
    uint32_t position;
    uint32_t total_frames;
    uint32_t cache_offset;
    size_t cache_bytes;
    bool initialized;
    _Alignas(32) uint8_t cache[KUI_CDDA_PCM_CACHE_BYTES];
};

enum kui_cdda_pcm_status kui_cdda_pcm_init(struct kui_cdda_pcm *,
    const struct kui_cdda_pcm_source *, kui_cdda_pcm_read_fn, void *ctx);
/* Seeking exactly to total_frames is legal. Invalid seeks preserve position. */
enum kui_cdda_pcm_status kui_cdda_pcm_seek(struct kui_cdda_pcm *, uint32_t frame);
/* Output is signed PCM16, 44100 Hz, one array per channel. Each array must hold
 * capacity frames and the arrays must not overlap. A final partial read returns
 * OK; EOF is returned when the call starts at EOF. capacity == 0 permits NULL
 * arrays and returns OK without storage access, including at EOF.
 *
 * done is required and is set to zero before validation. On IO or INVALID the
 * cursor is unchanged and output must be discarded; a retry starts at the same
 * frame. No partially decoded chunk is committed on a failed read. */
enum kui_cdda_pcm_status kui_cdda_pcm_read_frames(struct kui_cdda_pcm *,
    int16_t *left, int16_t *right, size_t capacity, size_t *done);
uint32_t kui_cdda_pcm_total_frames(const struct kui_cdda_pcm *);

#endif
