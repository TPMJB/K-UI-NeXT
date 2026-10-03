/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_cursor.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static struct kui_retail_manifest manifest;
static struct kui_retail_image image;
static uint8_t card[2048u * 512u], expected[64u * 2352u + 1], output[64u * 2352u + 1];
static unsigned checks, writes;
#define CHECK(test) do { ++checks; assert(test); } while(0)

/* The same deterministic track bytes as test_retail_image.c. */
static uint8_t source(uint32_t track, uint32_t file_byte) {
    uint32_t sector = file_byte / 2352u, inside = file_byte % 2352u;
    if(track != 1) {
        if(inside == 0 || inside == 11) return 0;
        if(inside < 11) return 255;
        if(inside == 15) return 1;
    }
    return (uint8_t)(track * 91u + sector * 53u + inside * 11u + (inside >> 8));
}
static int read_block(void *context, uint32_t lba, uint8_t out[512]) {
    (void)context;
    memcpy(out, card + lba * 512u, 512);
    return 0;
}
/* Extents of `take` blocks; scattered places them out of order on the card. */
static void fixture(unsigned take_max, bool scattered) {
    memset(&manifest, 0, sizeof(manifest));
    memset(card, 0xf3, sizeof(card));
    manifest.card_sectors = 2048;
    manifest.partition_start = 50; manifest.partition_end = 2000;
    manifest.track_count = 4;
    manifest.session_lba = 45000; manifest.boot_lba = 45001; manifest.boot_bytes = 4567;
    strcpy(manifest.title, "Cursor test"); strcpy(manifest.bootfile, "1ST_READ.BIN");
    static const uint32_t starts[4] = {0, 3, 45000, 45070};
    static const uint32_t ends[4] = {3, 5, 45070, 45072};
    uint32_t used = 0;
    for(unsigned i = 0; i < 4; ++i) {
        struct kui_retail_track *t = &manifest.tracks[i];
        *t = (struct kui_retail_track){.gd={.number=i + 1, .start_lba=starts[i],
            .end_lba=ends[i], .control=i == 1 ? 0u : 4u}, .first_extent=manifest.extent_count};
        uint32_t bytes = (t->gd.end_lba - t->gd.start_lba) * 2352u;
        uint32_t blocks = (bytes + 511u) / 512u;
        for(uint32_t n = 0; n < blocks;) {
            uint32_t take = blocks - n > take_max ? take_max : blocks - n;
            uint32_t index = manifest.extent_count;
            /* Scattered: odd extents far after even ones, with gaps. */
            uint32_t physical = scattered ? 100u + (index & 1u ? 900u : 0u) + (index >> 1) * (take_max + 1u) :
                100u + used;
            CHECK(index < KUI_RETAIL_IMAGE_EXTENTS && physical + take <= 1990u);
            manifest.extents[manifest.extent_count++] = (struct kui_retail_extent){n, physical, take};
            ++t->extent_count;
            for(uint32_t p = 0; p < take * 512u; ++p)
                if(n * 512u + p < bytes) card[physical * 512u + p] = source(i, n * 512u + p);
            n += take; used += take;
        }
    }
    CHECK(kui_retail_manifest_validate(&manifest) == KUI_GAME_OK);
    CHECK(kui_retail_image_init(&image, &manifest, read_block, card) == KUI_GAME_OK);
}
static void write_output(void *context, uint32_t offset, const uint8_t *bytes, uint32_t count) {
    CHECK(context == output && count && count <= 512u && offset + count <= sizeof(output) - 1);
    memcpy(output + offset, bytes, count);
    ++writes;
}
/* Feed the cursor block by block from the card and compare with the image
 * reader's output. Each run must stay within one extent of the map. */
static void compare(uint32_t lba, uint32_t count, enum kui_game_sector_format format) {
    if(kui_retail_image_check_validated(&manifest, lba, count, format) != KUI_GAME_OK) return;
    unsigned stride = format == KUI_GAME_SECTOR_RAW ? 2352u : 2048u;
    memset(expected, 0x77, sizeof(expected));
    CHECK(kui_retail_image_read(&image, lba, count, format, expected, sizeof(expected)) == KUI_GAME_OK);
    memset(output, 0x77, sizeof(output));
    struct kui_retail_cursor c;
    CHECK(kui_retail_cursor_begin(&c, &manifest, lba, count, format, write_output, output) == KUI_GAME_OK);
    unsigned fed = 0;
    while(c.done < count) {
        CHECK(c.run >= 1);
        bool inside = false;
        for(uint32_t i = 0; i < manifest.extent_count; ++i) {
            const struct kui_retail_extent *e = &manifest.extents[i];
            if(c.block >= e->card_lba && c.block - e->card_lba < e->blocks)
                inside = c.run <= e->blocks - (c.block - e->card_lba);
        }
        CHECK(inside);
        uint32_t before = c.done, block = c.block, run = c.run;
        CHECK(kui_retail_cursor_feed(&c, card + c.block * 512u) == KUI_GAME_OK);
        CHECK(c.done >= before && ++fed <= 64u * 6u);
        /* Within a run the next block follows on the card. */
        if(c.done < count && run > 1) CHECK(c.block == block + 1 && c.run == run - 1);
    }
    CHECK(c.done == count && !memcmp(output, expected, (size_t)count * stride));
    CHECK(output[count * stride] == 0x77);
}
static void sweep(void) {
    static const uint32_t firsts[] = {0, 1, 2, 3, 4, 45000, 45001, 45033, 45069, 45070, 45071};
    for(unsigned f = 0; f < sizeof(firsts) / sizeof(firsts[0]); ++f)
        for(uint32_t count = 1; count <= 64; ++count) {
            compare(firsts[f], count, KUI_GAME_SECTOR_MODE1);
            compare(firsts[f], count, KUI_GAME_SECTOR_RAW);
        }
}
static void mode_error(void) {
    fixture(3, true);
    /* A bad sync byte in sector 45001's header: sector 45000 completes first. */
    uint32_t file_byte = 2352u, lba = 0;
    const struct kui_retail_track *t = &manifest.tracks[2];
    for(uint32_t i = 0; i < t->extent_count; ++i) {
        const struct kui_retail_extent *e = &manifest.extents[t->first_extent + i];
        if(file_byte / 512u >= e->file_block && file_byte / 512u - e->file_block < e->blocks)
            lba = e->card_lba + file_byte / 512u - e->file_block;
    }
    card[lba * 512u + file_byte % 512u + 5u] = 0;
    struct kui_retail_cursor c;
    memset(output, 0x77, sizeof(output));
    CHECK(kui_retail_cursor_begin(&c, &manifest, 45000, 3, KUI_GAME_SECTOR_MODE1, write_output, output) == KUI_GAME_OK);
    enum kui_game_result r = KUI_GAME_OK;
    for(unsigned n = 0; n < 20 && r == KUI_GAME_OK && c.done < 3; ++n)
        r = kui_retail_cursor_feed(&c, card + c.block * 512u);
    CHECK(r == KUI_GAME_MODE && c.done == 1 && output[2048] == 0x77);
    for(unsigned i = 0; i < 2048; ++i) CHECK(output[i] == source(2, 16u + i));
}
int main(void) {
    static const unsigned takes[] = {3, 4, 5, 7, 64}; /* 128 extents at most */
    for(unsigned i = 0; i < sizeof(takes) / sizeof(takes[0]); ++i) {
        fixture(takes[i], false); sweep();
        fixture(takes[i], true); sweep();
    }
    mode_error();
    printf("retail cursor: %u checks, %u writes passed\n", checks, writes);
    return 0;
}
