/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_pcm.h"

enum kui_cdda_pcm_status kui_cdda_pcm_init(struct kui_cdda_pcm *c,
    const struct kui_cdda_pcm_source *source, kui_cdda_pcm_read_fn read, void *ctx) {
    if(!c) return KUI_CDDA_PCM_INVALID;
    c->initialized = false;
    c->cache_bytes = 0;
    if(!source || !read || !source->sectors ||
        (source->sector_stride != KUI_CDDA_PCM_SECTOR_BYTES && source->sector_stride != 2448u) ||
        source->backing_offset > source->file_bytes ||
        source->sectors > (source->file_bytes - source->backing_offset) / source->sector_stride ||
        source->sectors > UINT32_MAX / KUI_CDDA_PCM_FRAMES_PER_SECTOR)
        return KUI_CDDA_PCM_INVALID;
    c->source = *source;
    c->read = read;
    c->ctx = ctx;
    c->position = 0;
    c->total_frames = source->sectors * KUI_CDDA_PCM_FRAMES_PER_SECTOR;
    c->cache_offset = 0;
    c->initialized = true;
    return KUI_CDDA_PCM_OK;
}

enum kui_cdda_pcm_status kui_cdda_pcm_seek(struct kui_cdda_pcm *c, uint32_t frame) {
    if(!c || !c->initialized || frame > c->total_frames) return KUI_CDDA_PCM_INVALID;
    c->position = frame;
    return KUI_CDDA_PCM_OK;
}

/* All arithmetic here is bounded by init's complete-sector file check. Aligning
 * down may prefetch a prefix before this track, but never before the file. */
static bool read_frame(struct kui_cdda_pcm *c, uint32_t offset, uint8_t frame[4]) {
    unsigned copied = 0;
    while(copied < 4u) {
        if(!c->cache_bytes || offset < c->cache_offset ||
            offset - c->cache_offset >= c->cache_bytes) {
            c->cache_bytes = 0; /* A failed fill cannot expose an old cache. */
            c->cache_offset = offset & ~(KUI_CDDA_PCM_CACHE_BYTES - 1u);
            uint32_t bytes = c->source.file_bytes - c->cache_offset;
            if(bytes > KUI_CDDA_PCM_CACHE_BYTES) bytes = KUI_CDDA_PCM_CACHE_BYTES;
            if(!c->read(c->ctx, c->cache_offset, c->cache, bytes)) return false;
            c->cache_bytes = bytes;
        }
        uint32_t inside = offset - c->cache_offset;
        size_t take = c->cache_bytes - inside;
        if(take > 4u - copied) take = 4u - copied;
        for(size_t i = 0; i < take; ++i) frame[copied + i] = c->cache[inside + i];
        copied += (unsigned)take;
        offset += (uint32_t)take;
    }
    return true;
}

static int16_t sample(const uint8_t bytes[2], bool big_endian) {
    uint32_t bits = big_endian ? ((uint32_t)bytes[0] << 8) | bytes[1] :
        ((uint32_t)bytes[1] << 8) | bytes[0];
    /* Avoid implementation-defined conversion of an unsigned value > INT16_MAX. */
    return (int16_t)(bits < 32768u ? (int32_t)bits : (int32_t)bits - 65536);
}

enum kui_cdda_pcm_status kui_cdda_pcm_read_frames(struct kui_cdda_pcm *c,
    int16_t *left, int16_t *right, size_t capacity, size_t *done) {
    if(!done) return KUI_CDDA_PCM_INVALID;
    *done = 0;
    if(!c || !c->initialized || c->position > c->total_frames ||
        (capacity && (!left || !right)))
        return KUI_CDDA_PCM_INVALID;
    if(!capacity) return KUI_CDDA_PCM_OK;
    if(c->position == c->total_frames) return KUI_CDDA_PCM_EOF;
    uint32_t available = c->total_frames - c->position;
    size_t count = capacity > available ? available : capacity;
    uint32_t sector = c->position / KUI_CDDA_PCM_FRAMES_PER_SECTOR;
    uint32_t inside = c->position % KUI_CDDA_PCM_FRAMES_PER_SECTOR;
    uint32_t offset = c->source.backing_offset + sector * c->source.sector_stride + inside * 4u;
    for(size_t i = 0; i < count; ++i) {
        uint8_t frame[4];
        if(!read_frame(c, offset, frame)) return KUI_CDDA_PCM_IO;
        left[i] = sample(frame, c->source.big_endian);
        right[i] = sample(frame + 2, c->source.big_endian);
        offset += 4u;
        if(++inside == KUI_CDDA_PCM_FRAMES_PER_SECTOR) {
            inside = 0;
            /* The final sector needs no following offset. This also avoids an
             * unnecessary arithmetic wrap at the largest accepted file end. */
            if(i + 1u < count) offset += c->source.sector_stride - KUI_CDDA_PCM_SECTOR_BYTES;
        }
    }
    c->position += (uint32_t)count;
    *done = count;
    return KUI_CDDA_PCM_OK;
}

uint32_t kui_cdda_pcm_total_frames(const struct kui_cdda_pcm *c) {
    return c && c->initialized ? c->total_frames : 0;
}
