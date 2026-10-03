/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_ASYNC_H
#define KUI_RETAIL_ASYNC_H
#include "kui/retail_gd.h"
#include "kui/retail_cursor.h"
#include "retail_storage.h"
#include "retail_display.h"
#include "sci_stream.h"
#include <stddef.h>

/* Background game reader for SCI microSD. A read REQUEST starts a CMD18
 * stream; each block's DMA completion (or a receive error) interrupts the
 * game at the lowest level, and the handler checks the block, starts the
 * next one and copies the data into the game's destination. The game's own
 * GD calls also deliver whatever has arrived, and an EXEC waits for blocks
 * itself (as the ordinary reader does) while no interrupt is delivering.
 *
 * The interrupt reaches the reader through its own vector table placed in
 * front of the game's while a block is in flight: VBR+0x100 and +0x400 jump
 * straight to the game's vectors, and VBR+0x600 takes channel 1's DMTE1 and
 * the SCI's ERI/RXI, passing every other event to the game. The game's VBR
 * and interrupt levels are put back whenever the stream is idle. Games may
 * keep the bootstrap's VBR (0x8C00F400) throughout; it is hooked the same
 * way. Reads complete from the game's calls alone whenever no interrupt is
 * delivering. */
struct kui_retail_async_stats {
    uint32_t irq_blocks, call_blocks, waits, irqs, forwarded;
    uint32_t failures, max_retries, hooks, vbr_changes, boot_vbr;
};
struct kui_retail_async {
    const struct kui_retail_manifest *manifest;
    struct kui_retail_cursor cursor;
    uint32_t token, active, failed, destination, opened, retries, in_irq;
    uint32_t hooked, game_vbr, iprb, iprc, level, exec_irqs, exec_armed;
    uint32_t forward[3]; /* game VBR + 0x100, 0x400, 0x600: read by the vectors */
    struct kui_retail_async_stats stats;
};
/* State the resident shares with the reader, kept between the vectors. */
struct kui_retail_async_shared {
    struct kui_retail_gd service;
    struct kui_retail_storage card;
    struct retail_display_state display;
    struct kui_sci_stream_state stream;
};
/* VBR is this region's address minus 0x100; the hardware uses only its
 * three vector offsets, so the gaps between them hold the receive areas and
 * state. The vectors are copied from assembly templates at init. */
struct kui_retail_async_region {
    uint32_t vector100[8];                            /* VBR+0x100 */
    uint8_t area0[KUI_SCI_STREAM_AREA_BYTES] __attribute__((aligned(32)));
#ifdef KUI_RETAIL_ASYNC_TEST
    struct kui_retail_async engine;
    uint32_t vector400[8];
    struct kui_retail_async_shared shared;
#else
    union { struct kui_retail_async engine; uint8_t gap_a[0x300u - 0x240u]; };
    uint32_t vector400[8];                            /* VBR+0x400 */
    union { struct kui_retail_async_shared shared; uint8_t gap_b[0x500u - 0x320u]; };
#endif
    uint32_t vector600[16];                           /* VBR+0x600 */
    uint8_t area1[KUI_SCI_STREAM_AREA_BYTES] __attribute__((aligned(32)));
};
#ifndef KUI_RETAIL_ASYNC_TEST /* host pointers are wider; the layout is the console's */
_Static_assert(offsetof(struct kui_retail_async_region, area0) == 0x20u, "area0");
_Static_assert(offsetof(struct kui_retail_async_region, engine) == 0x240u, "engine");
_Static_assert(offsetof(struct kui_retail_async_region, vector400) == 0x300u, "vector 0x400");
_Static_assert(offsetof(struct kui_retail_async_region, shared) == 0x320u, "shared");
_Static_assert(offsetof(struct kui_retail_async_region, vector600) == 0x500u, "vector 0x600");
_Static_assert(offsetof(struct kui_retail_async_region, area1) == 0x540u, "area1");
_Static_assert(sizeof(struct kui_retail_async) <= 0xc0u, "engine fits gap A");
_Static_assert(sizeof(struct kui_retail_async_shared) <= 0x1e0u, "shared fits gap B");
#endif
extern struct kui_retail_async_region kui_retail_async_region;

/* Once, at resident init: install nothing yet, copy the vectors. */
void kui_retail_async_init(const struct kui_retail_manifest *);
/* Every GD call, masked on the private stack, before the service runs it. */
void kui_retail_async_call(uint32_t function);
/* After the service ran it: start a new read, or stop an abandoned one. */
void kui_retail_async_after(uint32_t function, int32_t result);
/* The handler behind VBR+0x600 for the stream's events: zero when handled,
 * nonzero to pass the event on to the game's own vector. */
uint32_t kui_retail_async_irq(void);
#endif
