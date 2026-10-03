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
 * stream; each block ends on the receiver's overrun, whose SCI error
 * interrupt (ERI) reaches the CPU at the lowest level, and the handler checks
 * the block, starts the next one and copies the data into the game's
 * destination. The channel raises no interrupt and the game's DMAC level is
 * never touched. The game's own GD calls also deliver whatever has arrived,
 * and an EXEC tops the blocks delivered since the previous EXEC up to the
 * ordinary reader's step, waiting itself where the interrupt did not.
 *
 * The interrupt reaches the reader through its own vector table placed in
 * front of the game's while a block is in flight: VBR+0x600 takes the SCI's
 * ERI/RXI; every other event, there or at VBR+0x100 and +0x400, first gives
 * the game its VBR and SCI level back and then enters the game's vector (the
 * releasing entries in retail_resident.S), so a game's handler never runs
 * under the reader's VBR (DOA2 crashes if it does). The next GD call installs
 * them again; with launch Y (manifest reader ASYNC_REHOOK) a released
 * interrupt's handler also returns through a trampoline that installs them
 * again at once. The game's VBR and level are put back whenever the stream
 * is idle. Games may keep the bootstrap's VBR (0x8C00F400) throughout; it is
 * hooked the same way. Reads complete from the game's calls alone whenever
 * no interrupt is delivering. */
struct kui_retail_async_stats {
    uint32_t irq_blocks, call_blocks, waits, irqs, failures;
    uint32_t max_retries, hooks, releases, vbr_changes;
};
/* Returns the trampoline can hold pending: interrupts released while an
 * earlier released one's handler still runs. */
#define KUI_RETAIL_ASYNC_RETURNS 3u
/* The releasing entries' and trampoline's state, at region + 0x240 with
 * these offsets (retail_resident.S). */
struct kui_retail_async_release {
    uint32_t vbr;    /* +0: the game's VBR the reader displaced */
    uint32_t sci;    /* +4: the game's IPRB SCI field (bits 7..4) */
    uint32_t armed;  /* +8: launch Y and streaming: re-hook on return */
    uint32_t depth;  /* +12: returns pending */
    /* +16: each pending return, oldest first: the interrupted PC (SPC)
     * and stack (SGR). */
    uint32_t pending[KUI_RETAIL_ASYNC_RETURNS][2];
    /* +40: shown as one row: returns that installed the reader again,
     * and events released at VBR+0x100, +0x400 and +0x600. */
    uint32_t rehooks, released[3];
};
struct kui_retail_async {
    struct kui_retail_async_release release; /* first: at region + 0x240 */
    const struct kui_retail_manifest *manifest;
    struct kui_retail_cursor cursor;
    uint32_t token, active, failed, destination, opened, retries, in_irq;
    uint32_t hooked, rehook, since_exec;
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
_Static_assert(offsetof(struct kui_retail_async, release) == 0 &&
               offsetof(struct kui_retail_async_release, armed) == 8u &&
               offsetof(struct kui_retail_async_release, depth) == 12u &&
               offsetof(struct kui_retail_async_release, pending) == 16u &&
               offsetof(struct kui_retail_async_release, rehooks) == 40u &&
               offsetof(struct kui_retail_async_release, released) == 44u,
               "release frame offsets used by retail_resident.S");
_Static_assert(sizeof(struct kui_retail_async_shared) <= 0x1e0u, "shared fits gap B");
#endif
extern struct kui_retail_async_region kui_retail_async_region;

/* Once, at resident init: install nothing yet, copy the vectors; the
 * manifest's reader chooses whether released interrupts re-hook on return. */
void kui_retail_async_init(const struct kui_retail_manifest *);
/* Every GD call, masked on the private stack, before the service runs it. */
void kui_retail_async_call(uint32_t function);
/* After the service ran it: start a new read, or stop an abandoned one. */
void kui_retail_async_after(uint32_t function, int32_t result);
/* The handler behind VBR+0x600 for the stream's events: zero when handled,
 * nonzero to pass the event on as any other (kui_retail_release_600). */
uint32_t kui_retail_async_irq(void);
/* The releasing entries (retail_resident.S): VBR and SCI level back to the
 * game's, then its vector at +0x100, +0x400 or +0x600; and the trampoline a
 * released interrupt's handler returns through with launch Y. */
void kui_retail_release_100(void);
void kui_retail_release_400(void);
void kui_retail_release_600(void);
void kui_retail_rehook(void);
#endif
