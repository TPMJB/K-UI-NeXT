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
    uint32_t blocks, polled, starts, stops, continued, kept;
    uint32_t overruns, crc_errors, token_errors, foreign, max_token_bytes;
    uint32_t repaired; /* overruns resumed in place, the lost byte rebuilt from the CRC */
};
/* Engine state, placed by the resident (sci_stream.c owns it otherwise). */
struct kui_sci_stream_state {
    const struct kui_loader_sd *card;
    uint8_t *area[2];
    uint32_t state, position, fill, ready_lba[2], kept_lba, saved[4];
    /* lost: the index of a byte an overrun lost (0: none), and the byte
     * the receiver held just before it. */
    uint16_t lost[2];
    /* kept: 1 + the area holding the last block taken, or 0. wire: the
     * area holds bit-reversed DMA bytes rather than programmed-read bytes. */
    uint8_t ready[2], wire[2], rdr[2], held[2], kept, sptr, irq;
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
 * token_limit bounds the token search in bytes. irq: the channel raises its
 * completion interrupt. OK: the DMA runs, or the block was received by
 * programmed transfers and is ready. */
enum kui_sci_stream_result kui_sci_stream_fetch(uint32_t lba, uint32_t token_limit, bool irq);
/* Whether a DMA is in flight. */
bool kui_sci_stream_busy(void);
/* If the in-flight block has arrived (or reception stopped), end it and hand
 * the SCI back: OK (the block is ready: kui_sci_stream_take), PENDING (still
 * arriving, or resumed after a mid-block overrun) or an error (the next
 * fetch restarts the stream). */
enum kui_sci_stream_result kui_sci_stream_poll(void);
/* Wait for the in-flight block (bounded; a stalled one ends as OVERRUN). */
enum kui_sci_stream_result kui_sci_stream_wait(void);
/* The 512 checked bytes of block lba, or NULL: *result is PENDING (not here)
 * or CRC (failed; the block must be fetched again). A block whose reception
 * overran once mid-block was resumed in place: exactly one byte was lost,
 * and it is rebuilt from the block's CRC16 (a second fault there is still
 * detected 255 times in 256 and refetched). A ready block is checked
 * once. The last block taken stays available, its bytes untouched by any
 * fetch, until another block is taken: a block shared by two requests is
 * not read twice, and a caller may fetch the next block before copying. */
const uint8_t *kui_sci_stream_take(uint32_t lba, enum kui_sci_stream_result *result);
/* Forget ready and kept blocks. */
void kui_sci_stream_discard(void);
/* Wait for any in-flight block, then CMD12: the card is idle. */
enum kui_sci_stream_result kui_sci_stream_stop(void);
const struct kui_sci_stream_stats *kui_sci_stream_stats(void);
#endif
