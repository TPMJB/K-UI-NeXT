/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef KUI_SCI_SD_BUS_H
#define KUI_SCI_SD_BUS_H
#include "sd_reader.h"

/* Retail Dreamcast SCI: TXD1=MOSI, RXD1=MISO, SCK1=clock, PA7=CS.
 * One device only, with bounded optional channel-1 DMA for aligned sectors.
 * DMA borrows only an idle channel and restores its registers with CPU IRQs
 * masked throughout; no interrupt handlers, heap or timers are installed.
 * Unsupported buffers or occupied DMA use the bounded programmed path.
 * The caller serializes access; snapshot/claim/release must run with IRQs masked.
 * Active SCI or unread data cause a no-write rejection. Every successful
 * claim must be released, even after protocol errors. This bus is shared by
 * the runtime and the freestanding game reader, linked independently. */
enum kui_loader_sd_result kui_sci_sd_acquire(void);
void kui_sci_sd_release(void);
const struct kui_loader_sd_bus *kui_sci_sd_bus(void);
bool kui_sci_sd_healthy(void);
#ifndef KUI_RETAIL_TRANSPORT
/* Resynchronize this lease's cached baud choice after a serialized runtime
 * borrower restores SCI registers. Caller holds IRQ masking and has verified
 * ownership/restoration. Read-only hardware validation, no wire traffic or
 * fault clearing; a stopped module is rejected before accessing SCI. */
bool kui_sci_sd_resync_speed(void);
/* First failed programmed flag wait in the current lease. Hardware values
 * are captured before stopping SCI; ssr is the failing loop's final sample,
 * not a later read. polls counts that sample (1..10000). Release and failed
 * acquire leave this evidence intact; successful acquire clears it. This
 * records wait_flag failures only, not every possible DMA/block failure. */
struct kui_sci_sd_fault {
    uint32_t valid, wait_flag, ssr, scr, smr, brr, scmr, sptr, pdtr, polls;
};
void kui_sci_sd_fault_get(struct kui_sci_sd_fault *out);
struct kui_sci_sd_stats {
    uint32_t rx_blocks, tx_blocks, polled_blocks, failures;
    uint32_t profiled_rx_blocks, profiled_tx_blocks;
    uint64_t rx_setup_us, rx_transfer_us, rx_check_us;
    uint64_t tx_setup_us, tx_transfer_us;
};
void kui_sci_sd_stats_get(struct kui_sci_sd_stats *out);
/* Optional runtime diagnostics, configured only between serialized transfers.
 * NULL disables timing. The callback must work with CPU interrupts masked.
 * Successful DMA samples accumulate independently of the lifetime counters:
 * setup starts after channel eligibility and includes purge/TX reversal;
 * transfer includes CPU feeding/CRC, wire waits and stopping SCI/DMA;
 * RX check measures the post-DMA reversal/CRC pass. Channel restoration and
 * SD commands/tokens/busy waits are outside these phases. No per-byte timing. */
void kui_sci_sd_profile_timer(uint64_t (*now_us)(void *), void *ctx);
#endif
#endif
