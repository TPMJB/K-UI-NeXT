/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_CURSOR_H
#define KUI_RETAIL_CURSOR_H
#include "kui/retail_image.h"

/* A validated image request, read one 512-byte card block at a time and in
 * order, so a reader can stop between blocks and continue later (from another
 * call or an interrupt). It produces exactly the bytes kui_retail_image_read
 * would: Mode1 checks each raw sector's 16-byte sync/mode header (it never
 * crosses a block, as 2352 and 512 are multiples of 16) and copies 2048 data
 * bytes; RAW copies all 2352. Every block between a request's first and last
 * needed byte holds some needed byte (the 288-byte Mode1 tail is shorter than
 * a block), so the needed blocks are consecutive within each track file; an
 * extent or track change moves the card address.
 *
 * The caller has already checked the request with
 * kui_retail_image_check_validated against the same immutable manifest.
 * write() receives each destination chunk (offset from the request's first
 * output byte) before the cursor moves on; it must not fail. */
typedef void (*kui_retail_cursor_write)(void *context, uint32_t offset,
    const uint8_t *bytes, uint32_t count);
struct kui_retail_cursor {
    const struct kui_retail_manifest *manifest;
    kui_retail_cursor_write write;
    void *context;
    uint32_t lba, count, raw;   /* request: first sector, sectors, 2352-byte output */
    uint32_t done;              /* complete sectors; done == count: request complete */
    uint32_t offset;            /* bytes of the current sector already delivered */
    uint32_t track;             /* manifest track index of the current sector */
    uint32_t block;             /* card LBA of the next block to feed */
    uint32_t run;               /* consecutive card blocks still needed from block, >= 1 */
};
/* RANGE if the map does not cover the first block (not for a checked request). */
enum kui_game_result kui_retail_cursor_begin(struct kui_retail_cursor *,
    const struct kui_retail_manifest *, uint32_t lba, uint32_t count,
    enum kui_game_sector_format, kui_retail_cursor_write, void *context);
/* Consume the card block at cursor->block: OK (block/run updated, or done ==
 * count), MODE for a sector without a Mode1 header, RANGE if the map does not
 * cover the next block. After an error the cursor must not be fed again. */
enum kui_game_result kui_retail_cursor_feed(struct kui_retail_cursor *, const uint8_t block[512]);
#endif
