/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SCI_ASYNC_PROBE_H
#define KUI_SCI_ASYNC_PROBE_H
#include <stdbool.h>
#include <stdint.h>

struct kui_loader_sd;
#define KUI_SCI_ASYNC_SLOW_TRIALS 16u
#define KUI_SCI_ASYNC_FAST_TRIALS 64u

enum kui_sci_async_status {
    KUI_SCI_ASYNC_OK = 0,
    KUI_SCI_ASYNC_ARGUMENT,
    KUI_SCI_ASYNC_BUSY,
    KUI_SCI_ASYNC_UNSUPPORTED,
    KUI_SCI_ASYNC_CANCELLED,
    KUI_SCI_ASYNC_COMMAND,
    KUI_SCI_ASYNC_TOKEN,
    KUI_SCI_ASYNC_TIMEOUT,
    KUI_SCI_ASYNC_RECEIVE_ERROR,
    KUI_SCI_ASYNC_DMA_ERROR,
    KUI_SCI_ASYNC_GUARD,
    KUI_SCI_ASYNC_CRC,
    KUI_SCI_ASYNC_MISMATCH,
    KUI_SCI_ASYNC_RESTORE,
    KUI_SCI_ASYNC_NO_OVERLAP,
    KUI_SCI_ASYNC_HANDOFF,
    KUI_SCI_ASYNC_BUS_FAULT
};

enum kui_sci_async_phase {
    KUI_SCI_ASYNC_PHASE_NONE = 0,
    KUI_SCI_ASYNC_PHASE_LEASE,
    KUI_SCI_ASYNC_PHASE_BUFFER,
    KUI_SCI_ASYNC_PHASE_READY,
    KUI_SCI_ASYNC_PHASE_COMMAND,
    KUI_SCI_ASYNC_PHASE_TOKEN,
    KUI_SCI_ASYNC_PHASE_TOKEN_END,
    KUI_SCI_ASYNC_PHASE_OWNERSHIP,
    KUI_SCI_ASYNC_PHASE_GPIO,
    KUI_SCI_ASYNC_PHASE_DMA,
    KUI_SCI_ASYNC_PHASE_VALIDATE,
    KUI_SCI_ASYNC_PHASE_COMPLETE,
    KUI_SCI_ASYNC_PHASE_HANDOFF
};

/* Exact ordinary-bus operation underway when a sticky framing fault occurs.
 * The index is zero-based within command/response/token/ready byte loops. */
enum kui_sci_async_framing_step {
    KUI_SCI_ASYNC_FRAMING_NONE = 0,
    KUI_SCI_ASYNC_FRAMING_DESELECT,
    KUI_SCI_ASYNC_FRAMING_IDLE_CLOCK,
    KUI_SCI_ASYNC_FRAMING_SELECT,
    KUI_SCI_ASYNC_FRAMING_READY,
    KUI_SCI_ASYNC_FRAMING_COMMAND,
    KUI_SCI_ASYNC_FRAMING_RESPONSE,
    KUI_SCI_ASYNC_FRAMING_TOKEN,
    KUI_SCI_ASYNC_FRAMING_HANDOFF
};

struct kui_sci_async_stage {
    uint32_t clock_hz, attempted, passed;
    enum kui_sci_async_phase last_phase;
    uint32_t dma_started, command_response, last_token;
    /* Pin samples and status captured before cleanup, not output-latch reads. */
    uint32_t snapshot_ssr, snapshot_sptr;
    uint32_t dma_irqs, sci_error_irqs, unexpected_rx_irqs;
    uint32_t trailing_overruns, premature_errors, timeouts;
    uint32_t overlap_batches, overlap_iterations, work_checksum;
    uint32_t last_remaining, last_chcr, last_ssr;
    uint32_t handoff_checks, handoff_retries, handoff_failures;
    uint32_t handoff_ssr, handoff_scr, handoff_sptr;
    uint32_t bus_faults;
    enum kui_sci_async_framing_step framing_step;
    uint32_t framing_index;
    /* The normal bus's first failed wait, sampled before it disables SCI.
     * Independent from snapshot_ssr, which may show the later stopped state. */
    uint32_t bus_fault_valid, bus_wait_flag, bus_fault_ssr, bus_fault_scr;
    uint32_t bus_fault_smr, bus_fault_brr, bus_fault_scmr, bus_fault_sptr;
    uint32_t bus_fault_pdtr, bus_fault_polls;
    /* receive_us includes start-to-worker-observation/cleanup overhead;
     * it is not payload wire time or maximum CPU blocking time. */
    uint64_t elapsed_us, receive_us, max_receive_us;
};

struct kui_sci_async_probe_result {
    enum kui_sci_async_status status;
    enum kui_sci_async_status operation_status; /* Before cleanup classification. */
    uint32_t lba;
    struct kui_sci_async_stage slow, fast;
    uint64_t elapsed_us, max_irq_masked_us, max_irq_handler_us;
    bool started, safe_restored, guards_ok, crc_ok, baseline_ok;
    bool handlers_restored, registers_restored;
    bool dma_quarantined, foreign_dma;
    /* This probe measures work during DMA and its own IRQ delivery only. */
    bool timer_irq_instrumented;
};

/* Isolated runtime diagnostic, never a filesystem or resident reader.
 * Caller owns and serializes the existing SCI session, supplies a CRC-checked
 * baseline, and verifies recovery with a normal read before saving a report.
 * Requires ready/fast card, interrupts enabled, an idle channel 1 and an
 * already-enabled DMAC interrupt priority. Both SCI GPIO directions must be
 * inputs because sampled pin reads cannot preserve foreign output latches.
 * Does not change global DMAOR or
 * the shared DMAC priority. Cancellation is checked between bounded trials.
 * safe_restored describes local hardware/IRQ cleanup, not card recovery.
 * An incomplete DMA has no documented abort-drain acknowledgement: its static
 * destination and channel are quarantined, safe_restored=false, until restart.
 * The selected card is deselected on every started exit. */
enum kui_sci_async_status kui_sci_async_probe_run(
    const struct kui_loader_sd *card, uint32_t lba,
    const uint8_t baseline[512], bool (*cancelled)(void *), void *cancel_ctx,
    struct kui_sci_async_probe_result *out);
const char *kui_sci_async_status_name(enum kui_sci_async_status status);
const char *kui_sci_async_phase_name(enum kui_sci_async_phase phase);
const char *kui_sci_async_framing_name(enum kui_sci_async_framing_step step);
#endif
