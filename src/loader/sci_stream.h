/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SCI_STREAM_H
#define KUI_SCI_STREAM_H
#include "sd_reader.h"

/* Game-reader CMD18 streaming over SCI with receive-only DMA on channel 1.
 * Each block is 513 bytes of DMA; the receiver then takes the second CRC byte
 * into RDR and stops on the next byte's overrun, so only the card's one-byte
 * gap is lost. Between blocks: deselect, SCI module reset (MSTP0) and
 * re-initialization, then reselection, where the card resumes with the next
 * block's data token. The stream stays open between calls; the card simply
 * waits while it is not clocked. Two receive areas let one block be checked
 * while the next one is received. If channel 1 is not idle when a block's
 * token has arrived, that block is received by programmed transfers instead.
 *
 * Every function runs with interrupts masked (a game-service call) or in the
 * reader's own interrupt handler (SR.BL set); none waits for a DMA except
 * kui_sci_stream_stop and kui_sci_stream_wait. All card waits are bounded by
 * byte counts. The bus must be acquired and the card ready at fast speed. */
#define KUI_SCI_STREAM_AREA_BYTES 544u /* 513 DMA bytes in whole cache lines */
enum kui_sci_stream_result {
    KUI_SCI_STREAM_OK, KUI_SCI_STREAM_PENDING,
    KUI_SCI_STREAM_BUSY,     /* channel 1 or the SCI was taken over: not ours */
    KUI_SCI_STREAM_COMMAND,  /* CMD18/CMD12 rejected or unanswered */
    KUI_SCI_STREAM_TOKEN,    /* no data token (wrong byte or wait limit) */
    KUI_SCI_STREAM_CRC,      /* a received block failed its CRC16 */
    KUI_SCI_STREAM_OVERRUN,  /* reception stopped before the block's end */
    KUI_SCI_STREAM_RESET     /* SCI or bus did not return to a known state */
};
struct kui_sci_stream_stats {
    uint32_t blocks, polled, starts, kept, overruns;
    uint32_t crc_errors, token_errors, foreign;
    uint32_t repaired; /* overruns resumed in place, the lost byte rebuilt from the CRC */
    /* ahead: repaired blocks whose last byte was the card's gap or next
     * token: the resumed reception ran a byte ahead (fetched again). */
    uint32_t ahead;
    uint32_t deferred; /* of those repaired, resumed by a later fetch (interrupt) */
#ifdef KUI_RETAIL_CE
    /* Incomplete receptions, including repair attempts, by DMA bytes still
     * remaining: 0, 1..128, 129..384, 385 or more. These are not timings. */
    uint32_t incomplete[4];
    /* Read-only snapshots after the existing stop/handoff, not at the
     * instant of failure; correlation alone does not establish contention. */
    uint32_t incomplete_ch2_active, incomplete_dmaor_bad;
    uint32_t token_bytes, token_max; /* all search bytes, including token/error */
    uint32_t stops; /* CMD12 attempts */
    uint32_t token_yields; /* token search paused at an entry's byte budget */
#endif
};
/* Engine state, placed by the resident (sci_stream.c owns it otherwise). */
struct kui_sci_stream_state {
    const struct kui_loader_sd *card;
    uint8_t *area[2];
    /* arrived: 1 + the block last received, ready, while no other reception
     * has started since (0: none). */
    uint32_t state, position, fill, ready_lba[2], kept_lba, saved[4], arrived;
    /* lost: 1 + the index of a byte an overrun lost (0: none); hold: the
     * receiver still held the byte before it (held), which the channel
     * never took. */
    uint16_t lost[2];
    /* kept: 1 + the area holding the last block taken, or 0. wire: the
     * area holds bit-reversed DMA bytes rather than programmed-read bytes.
     * unrepaired: repaired blocks that failed their CRC for an unknown reason
     * (not a byte ahead); after two the card is taken not to resume
     * mid-block as expected, and overruns restart. */
    uint8_t ready[2], wire[2], rdr[2], held[2], hold[2], kept, sptr, unrepaired;
#ifdef KUI_RETAIL_CE
    uint8_t token_bounded, token_polled;
    uint32_t token_limit, token_used, token_budget;
#endif
    struct kui_sci_stream_stats stats;
};
/* Adopt the bus (acquired, card ready) and two 32-byte-aligned receive areas
 * of KUI_SCI_STREAM_AREA_BYTES that nothing else writes. unknown: the card
 * may still be streaming (after a bus fault), so the first fetch stops it
 * first. RESET if the SCI is not in the bus's fast synchronous state. */
enum kui_sci_stream_result kui_sci_stream_open(const struct kui_loader_sd *card,
    uint8_t *area0, uint8_t *area1, bool unknown);
/* Start receiving card block lba: continue the open stream when it is at lba,
 * otherwise stop it (CMD12) and issue CMD18 there. No DMA may be in flight.
 * token_limit bounds the token search in bytes. The channel raises no
 * interrupt; a reception ends on the receiver's overrun, which raises the
 * SCI's ERI wherever its level allows. polled: receive by programmed
 * transfers (masked, about 0.7 ms, cannot overrun) instead of DMA. OK: the
 * DMA runs, or the block was received by programmed transfers and is ready. */
enum kui_sci_stream_result kui_sci_stream_fetch(uint32_t lba, uint32_t token_limit, bool polled);
/* Whether a DMA is in flight. */
bool kui_sci_stream_busy(void);
/* Whether the block last received is lba, arrived and not yet taken (nothing
 * in flight): the next one may be fetched before it is taken, and goes into
 * the other area. */
bool kui_sci_stream_ready(uint32_t lba);
#ifdef KUI_RETAIL_CE
#define KUI_SCI_STREAM_TOKEN_SLICE 256u
/* Once per external service entry, shared by its before/after work. Before
 * CE's interrupt service is installed, token searches remain unbounded by
 * this slice (their original total byte limit still applies). */
void kui_sci_stream_token_budget(bool bounded);
bool kui_sci_stream_token_pending(void);
#endif
/* If the in-flight block has arrived (or reception stopped), end it and hand
 * the SCI back: OK (the block is ready: kui_sci_stream_take), PENDING (still
 * arriving, or resumed after a mid-block overrun) or an error (the next
 * fetch restarts the stream). interrupt: a mid-block overrun is not resumed
 * now but left for the next fetch of that block (PENDING, not busy): repairs
 * resumed from the game reader's interrupt, at the overrun, failed on the
 * console where those resumed later did not. */
enum kui_sci_stream_result kui_sci_stream_poll(bool interrupt);
/* Wait for the in-flight block (bounded; a stalled one ends as OVERRUN). */
enum kui_sci_stream_result kui_sci_stream_wait(void);
/* The 512 checked bytes of block lba, or NULL: *result is PENDING (not here)
 * or CRC (failed; the block must be fetched again). A block whose reception
 * overran once mid-block was resumed in place: exactly one byte was lost,
 * and it is rebuilt from the block's CRC16 (a second fault there is still
 * detected 255 times in 256 and refetched). If its last byte is the card's
 * gap or next token instead of the second CRC byte, the reception ran a
 * byte ahead and the block is refetched without a rebuild, which could
 * otherwise accept it 1 time in 256. A ready block is checked
 * once. The last block taken stays available, its bytes untouched by any
 * fetch, until another block is taken (or a block received before it is
 * taken needs its area): a block shared by two requests is not read twice,
 * and a caller may fetch the next block before copying. */
const uint8_t *kui_sci_stream_take(uint32_t lba, enum kui_sci_stream_result *result);
/* Forget ready and kept blocks. */
void kui_sci_stream_discard(void);
/* Wait for any in-flight block, then CMD12: the card is idle. */
enum kui_sci_stream_result kui_sci_stream_stop(void);
const struct kui_sci_stream_stats *kui_sci_stream_stats(void);
#endif
