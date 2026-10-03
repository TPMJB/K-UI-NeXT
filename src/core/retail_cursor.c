/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_cursor.h"

static uint32_t needed(const struct kui_retail_cursor *c) {
    return c->raw ? KUI_GAME_RAW_BYTES : 16u + KUI_GAME_DATA_BYTES;
}
/* The card block holding the next needed byte, and how many consecutive card
 * blocks from it the request still needs within this extent and track. */
static enum kui_game_result locate(struct kui_retail_cursor *c) {
    const struct kui_retail_manifest *m = c->manifest;
    uint32_t sector = c->lba + c->done;
    while(m->tracks[c->track].gd.end_lba <= sector) ++c->track;
    const struct kui_retail_track *t = &m->tracks[c->track];
    uint32_t file_block = ((sector - t->gd.start_lba) * KUI_GAME_RAW_BYTES + c->offset) / 512u;
    uint32_t last = c->lba + c->count;
    if(last > t->gd.end_lba) last = t->gd.end_lba;
    uint32_t end_block = ((last - 1u - t->gd.start_lba) * KUI_GAME_RAW_BYTES + needed(c) + 511u) / 512u;
    uint32_t lo = t->first_extent, hi = lo + t->extent_count;
    while(lo + 1u < hi) {
        uint32_t mid = lo + (hi - lo) / 2u;
        if(m->extents[mid].file_block <= file_block) lo = mid;
        else hi = mid;
    }
    const struct kui_retail_extent *e = &m->extents[lo];
    if(file_block < e->file_block || file_block - e->file_block >= e->blocks) return KUI_GAME_RANGE;
    uint32_t stop = e->file_block + e->blocks;
    if(stop > end_block) stop = end_block;
    c->block = e->card_lba + (file_block - e->file_block);
    c->run = stop - file_block;
    return KUI_GAME_OK;
}
enum kui_game_result kui_retail_cursor_begin(struct kui_retail_cursor *c,
    const struct kui_retail_manifest *m, uint32_t lba, uint32_t count,
    enum kui_game_sector_format format, kui_retail_cursor_write write, void *context) {
    *c = (struct kui_retail_cursor){.manifest = m, .write = write, .context = context,
        .lba = lba, .count = count, .raw = format == KUI_GAME_SECTOR_RAW};
    return locate(c);
}
enum kui_game_result kui_retail_cursor_feed(struct kui_retail_cursor *c, const uint8_t block[512]) {
    const struct kui_retail_track *t = &c->manifest->tracks[c->track];
    uint32_t base = (c->lba + c->done - t->gd.start_lba) * KUI_GAME_RAW_BYTES;
    uint32_t start = (base + c->offset) & ~511u, end = start + 512u;
    for(;;) {
        uint32_t position = base + c->offset;
        if(position >= end) break;
        const uint8_t *in = block + (position - start);
        if(!c->raw && !c->offset) {
            if(in[0] || in[11] || in[15] != 1) return KUI_GAME_MODE;
            for(unsigned i = 1; i < 11; ++i) if(in[i] != 255) return KUI_GAME_MODE;
            c->offset = 16u;
            continue;
        }
        uint32_t take = needed(c) - c->offset;
        if(take > end - position) take = end - position;
        uint32_t output = c->raw ? c->done * KUI_GAME_RAW_BYTES + c->offset :
            c->done * KUI_GAME_DATA_BYTES + c->offset - 16u;
        c->write(c->context, output, in, take);
        c->offset += take;
        if(c->offset < needed(c)) break;
        c->offset = 0;
        if(++c->done == c->count) return KUI_GAME_OK;
        if(c->lba + c->done >= t->gd.end_lba) break; /* next sector: next track file */
        base += KUI_GAME_RAW_BYTES;
    }
    return locate(c);
}
