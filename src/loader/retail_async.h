/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_ASYNC_H
#define KUI_RETAIL_ASYNC_H
#include "kui/retail_gd.h"
#include "kui/retail_cursor.h"
#include "retail_storage.h"
#include "retail_display.h"
#include "sci_stream.h"
#include "kui/native_background_budget.h"
#include "sci_sd_bus.h"
#include "kui/armada_ce_vmu_sd_loan.h"
#include "kui/vmu_native_async_loan.h"
#include <stddef.h>

/* Background game reader for SCI microSD. A read REQUEST starts a CMD18
 * stream; each block ends on the receiver's overrun, whose SCI error
 * interrupt (ERI) reaches the CPU at the lowest level, and the handler checks
 * the block, starts the next one and copies the data into the game's
 * destination. The channel raises no interrupt and the game's DMAC level is
 * never touched. The game's own GD calls also deliver whatever has arrived.
 * An EXEC or CHECK tops the blocks delivered since the previous one up to a
 * target, waiting for the rest itself: 20 blocks (about twice the ordinary
 * reader's step) with launch X, 25 with Y (manifest reader ASYNC_EAGER).
 * Each block of a run is checked and copied while the next one arrives.
 *
 * The interrupt reaches the reader through its own vector table placed in
 * front of the game's while a block is in flight: VBR+0x600 takes the SCI's
 * ERI/RXI; every other event, there or at VBR+0x100 and +0x400, first gives
 * the game its VBR and SCI level back and then enters the game's vector (the
 * releasing entries in retail_resident.S), so a game's handler never runs
 * under the reader's VBR (DOA2 crashes if it does). An interrupt's handler
 * also returns through a trampoline that installs them again at once; the
 * next GD call does so after an exception. The game's VBR and level are put
 * back whenever the stream is idle. Games may keep the bootstrap's VBR
 * (0x8C00F400) throughout; it is hooked the same way. Reads complete from
 * the game's calls alone whenever no interrupt is delivering.
 *
 * Windows CE (built with KUI_RETAIL_CE): CE owns the exception vectors, and
 * its own interrupt dispatch reaches the reader through the handler table
 * entries the resident installs for the SCI's events (kui_retail_ce_isr);
 * only the SCI's level changes here, and only once they are installed
 * (isr). A read's output goes into pieces: a PIOREAD's or DMAREAD's whole
 * destination, or each transfer CE's driver gives a stream (DMA_TRANSFER;
 * PIO_TRANSFER through kui_retail_async_read_part). The interrupt fills only
 * pieces at physical addresses (P1, P2 or a DMA destination); CE's virtual
 * addresses are filled by its own calls. The interrupt prefetches those
 * bytes, and bytes past a piece's end, into a bounded physical ring; calls
 * drain it in order. Card production pauses when the ring is full, and a
 * queued tail never completes a request before its destination is filled.
 * A full stream transfer raises the DMA end interrupt and a
 * finished request the drive's (kui_retail_gd_stream_progress,
 * kui_retail_gd_progress); a block the interrupt cannot place raises the
 * drive's too, so CE's driver calls again to take it. */
struct kui_retail_async_stats {
    /* In the separate stall diagnostic execs/exec_int hold foreground/IRQ
     * delivery-visit raw TMU0 maxima, reset by the serialized worker.
     * REQUEST_TIMING instead uses hooks/releases for request-start ticks
     * and longest completed-request ticks; legacy hook counts are omitted.
     * IRQ_REARM uses execs/exec_int for SCI-level repairs and real IRQ entries. */
#if KUI_NATIVE_GD_MMU_BUFFERS
    /* This private foreground profile elides every counter write. Keep the
     * named zero-valued diagnostic view without reserving seven words. */
    union {
        uint32_t irq_blocks,call_blocks,waits,execs,exec_int,hooks,releases;
    };
#else
    uint32_t irq_blocks, call_blocks, waits, execs, exec_int;
    uint32_t hooks, releases;
#endif
};
/* Returns the trampoline can hold pending: interrupts released while an
 * earlier released one's handler still runs. */
#define KUI_RETAIL_ASYNC_RETURNS 3u
#if KUI_NATIVE_LOADER_TIMELINE
void kui_retail_async_trace(uint32_t exit,uint32_t function);
#endif
/* The releasing entries' and trampoline's state, at region + 0x240 with
 * these offsets (retail_resident.S). */
struct kui_retail_async_release {
#if KUI_NATIVE_GD_LEGACY_STREAM && KUI_NATIVE_STANDALONE_FOREGROUND && !defined(KUI_RETAIL_ASYNC_TEST)
    /* No raw-vector trampoline exists in this private foreground profile.
     * Retain its SCI ownership and depth guard, without inactive frames. */
    uint32_t sci,armed,depth;
#else
    uint32_t vbr;    /* +0: the game's VBR the reader displaced */
    uint32_t sci;    /* +4: the game's IPRB SCI field (bits 7..4) */
    uint32_t armed;  /* +8: streaming: a return installs the reader again */
    uint32_t depth;  /* +12: returns pending */
    /* +16: each pending return, oldest first: the interrupted PC (SPC)
     * and stack (SGR). */
    uint32_t pending[KUI_RETAIL_ASYNC_RETURNS][2];
    /* +40: shown as one row: returns that installed the reader again,
     * events released at VBR+0x100, +0x400 and +0x600, and (C only) GD
     * calls that found the game had moved to other vectors. */
    uint32_t rehooks, released[3], vbr_changes;
#endif
};
struct kui_retail_async {
    struct kui_retail_async_release release; /* first: at region + 0x240 */
    const struct kui_retail_manifest *manifest;
    struct kui_retail_cursor cursor;
    /* since: blocks delivered since the previous EXEC or CHECK; target: the
     * blocks each of them makes sure of. */
    uint32_t token, destination, since;
#if KUI_NATIVE_SESSION_CONTROLS
    uint32_t disc_generation; /* Snapshotted before any destination copy. */
#endif
    uint32_t active, failed, opened, retries, in_irq, hooked, target;
    struct kui_retail_async_stats stats;
#if KUI_VMU_NATIVE_BACKGROUND
    /* Fits the unused native vector gap; release-frame offsets stay fixed. */
    uint32_t native_loan_state;
#endif
#if KUI_NATIVE_REQUEST_TIMING && !KUI_NATIVE_GD_MMU_BUFFERS
    /* Exactly the eight spare bytes of the native vector gap. The MMU
     * foreground profile neither samples nor displays these maxima. */
    uint32_t native_max_lba,native_max_count;
#endif
#ifdef KUI_RETAIL_CE
    /* The current piece: request output bytes [piece_begin, piece_begin +
     * piece_bytes) go to guest address piece_destination (or, from
     * read_part, to piece_direct); piece_filled of them are written. Once
     * closed (piece_set clear), or virtual during an interrupt, output goes
     * to the spill ring: spill_bytes from spill_from, wrapping at its end.
     * They are the bytes immediately after the piece's delivered bytes.
     * active tracks card production, not delivery: a final queued tail may
     * remain after it clears. isr: CE's
     * handler table leads the SCI's events to the resident. */
    uint32_t piece_destination, piece_begin, piece_bytes, piece_filled;
    uint32_t spill_from, spill_bytes;
    uint32_t queue_highwater, prefetched_bytes, queue_blocked;
    /* Work per delivery visit, not elapsed time or a whole GD call. */
    uint32_t polled_irq_max, polled_call_max;
    uint32_t pio_calls, pio_bytes; /* token-matching read_part requests */
    uint8_t *piece_direct;
    /* A failed overrun uses a polled reread of this block only. Other
     * faults retain the ordinary retry threshold; no session-wide mode. */
    uint8_t piece_set, piece_physical, isr, early_polled;
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME
    /* Canonical P1 state, shared by masked reader calls/IRQs and the fixed
     * worker loan entry. No guest pointer or pending caller frame is kept. */
    volatile uint32_t vmu_loan_state;
    uint32_t vmu_loan_generation;
#endif
#endif
};
/* State the resident shares with the reader, kept between the vectors (the
 * SCI bus's own state too: the resident's BSS has no room for it). */
struct kui_retail_async_shared {
    struct kui_retail_gd service;
    struct kui_retail_storage card;
    struct retail_display_state display;
    struct kui_sci_stream_state stream;
    struct kui_sci_sd_port port;
};
#ifdef KUI_RETAIL_CE
/* Bounded physical staging while CE runs or supplies the next destination.
 * Power of two for ring indexing; whole card blocks keep admission simple. */
#define KUI_RETAIL_ASYNC_SPILL_BYTES 2048u
struct kui_retail_async_region {
    uint8_t area0[KUI_SCI_STREAM_AREA_BYTES] __attribute__((aligned(32)));
    uint8_t area1[KUI_SCI_STREAM_AREA_BYTES] __attribute__((aligned(32)));
    uint8_t spill[KUI_RETAIL_ASYNC_SPILL_BYTES];
    struct kui_retail_async engine;
    struct kui_retail_async_shared shared;
};
#elif KUI_NATIVE_GD_LEGACY_STREAM && KUI_NATIVE_STANDALONE_FOREGROUND && !defined(KUI_RETAIL_ASYNC_TEST)
/* This foreground-only profile publishes no vectors or SDK callbacks. Keep
 * the same two aligned receive buffers and typed engine/service state, but
 * omit raw-vector holes. No external code addresses these private offsets. */
struct kui_retail_async_region {
    uint8_t area0[KUI_SCI_STREAM_AREA_BYTES] __attribute__((aligned(32)));
    uint8_t area1[KUI_SCI_STREAM_AREA_BYTES] __attribute__((aligned(32)));
    struct kui_retail_async engine;
    struct kui_retail_async_shared shared;
};
/* MMU-only counter compaction may reduce private state; it cannot enlarge
 * the original foreground reservation. Exact new offsets are build-bound. */
_Static_assert(
#if KUI_NATIVE_GD_MMU_BUFFERS
    sizeof(struct kui_retail_async_region)==(KUI_NATIVE_GD_DMA_STREAM?0x640u:0x660u) &&
    offsetof(struct kui_retail_async_region,shared)==0x4acu &&
#else
    sizeof(struct kui_retail_async_region)==0x6a0u &&
    offsetof(struct kui_retail_async_region,shared)==0x4ccu &&
#endif
    _Alignof(struct kui_retail_async_region)==32u &&
    offsetof(struct kui_retail_async_region,area0)==0u &&
    offsetof(struct kui_retail_async_region,area1)==0x220u &&
    offsetof(struct kui_retail_async_region,engine)==0x440u &&
    sizeof(struct kui_retail_async_release)==12u,
    "Private foreground region ABI changed");
#else
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
#endif
typedef struct kui_retail_async_region kui_retail_async_region_type;
#if KUI_VMU_NATIVE_BACKGROUND || KUI_NATIVE_STANDALONE_READER
/* The stage supplies the initial protected workspace; the native allocator
 * binder moves the complete idle region into the already owned lease tail. */
extern kui_retail_async_region_type *kui_retail_async_region_storage;
#define kui_retail_async_region (*kui_retail_async_region_storage)
#else
extern struct kui_retail_async_region kui_retail_async_region;
#endif

/* Once, at resident init: install nothing yet, copy the vectors; the
 * manifest's reader chooses how long GD calls may wait for blocks. */
void kui_retail_async_init(const struct kui_retail_manifest *);
#if KUI_SMASH_MENU_LOADER
/* Surviving raw capsule reader: each bounded consecutive run owns SCI once,
 * checks every block's CRC16 and tears down before returning. The caller
 * owns count*512 nonaliasing destination bytes; at most4096 blocks fit. */
uint32_t kui_vc2_ce_vmu_read_card(uint32_t card_lba,uint8_t out[512],uint32_t count);
#elif KUI_VC2_VMU_LATE
/* Exact late-VC2 caller only: IRQs masked, the completed native stream is
 * idle, and out is an owned nonaliasing512-byte destination. Read one mapped
 * helper-file card block without changing the GD token/command/byte cursor.
 * Returns enum kui_sci_stream_result (zero on success). Preflight refusal
 * is BUSY; an uncertain acquire/stop/release is RESET and stays quarantined. */
uint32_t kui_vc2_ce_vmu_read_card(uint32_t card_lba,uint8_t out[512]);
#endif
#if KUI_NATIVE_SESSION_CONTROLS
/* Masked native worker only, with no pending request/receiver/return frame.
 * RESET invalidates all read-ahead bytes while its loan state is still IDLE,
 * before the existing writer loan closes SCI. Returns1 claimed,0 busy,-1
 * context/transport fault. End the successful loan with the ordinary native
 * loan_end; never resume a game or begin another request while it is owned. */
int kui_retail_async_session_begin(struct kui_loader_sd *loan);
/* Rebind a newly selected, prevalidated owned map after GD disc_close and
 * while the successful session loan remains WRITER. Clears retired cursor
 * and request state without disturbing the SDK vectors/release metadata. */
void kui_retail_async_session_rebind(const struct kui_retail_manifest *next);
#endif
/* KUI_ARMADA_CE_STREAM adds a checked static SCI descriptor and bounded
 * arbitration visits. The GD engine/cursor/token/spill remain DATA-owned;
 * the high controller owns audio jobs and any parked checked blocks. */
/* Every GD call, masked on the private stack, before the service runs it. */
void kui_retail_async_call(uint32_t function);
/* After the service ran it: start a new read, or stop an abandoned one. */
void kui_retail_async_after(uint32_t function, int32_t result);
/* The handler behind VBR+0x600 for the stream's events: zero when handled,
 * nonzero to pass the event on as any other (kui_retail_release_600). Under
 * Windows CE, CE's dispatch calls it for the SCI's events; nonzero: none was
 * the stream's (its level is dropped). */
uint32_t kui_retail_async_irq(void);
#ifdef KUI_RETAIL_CE
/* The GD service's read_part for a PIO stream (Windows CE): bytes [skip,
 * skip + bytes) of the request's output, the next ones, into output (a CE
 * virtual buffer, written only here, in the caller's call). Waits for the
 * blocks it needs; zero once all were written. */
int kui_retail_async_read_part(void *unused, uint32_t lba, uint32_t sector_bytes,
                               uint32_t skip, uint32_t bytes, void *output);
#else
/* The releasing entries (retail_resident.S): VBR and SCI level back to the
 * game's, then its vector at +0x100, +0x400 or +0x600; and the trampoline a
 * released interrupt's handler returns through. */
void kui_retail_release_100(void);
void kui_retail_release_400(void);
void kui_retail_release_600(void);
void kui_retail_rehook(void);
#endif
#endif
