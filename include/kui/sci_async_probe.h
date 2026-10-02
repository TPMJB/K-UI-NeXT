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
    KUI_SCI_ASYNC_BUS_FAULT,
    KUI_SCI_ASYNC_PENDING
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
    KUI_SCI_ASYNC_FRAMING_HANDOFF,
    /* Clocking out the rest of a block interrupted by a receive overrun. */
    KUI_SCI_ASYNC_FRAMING_DRAIN
};

enum kui_sci_async_module_reset_state {
    KUI_SCI_ASYNC_MODULE_RESET_NONE = 0,
    KUI_SCI_ASYNC_MODULE_RESET_OK,
    KUI_SCI_ASYNC_MODULE_RESET_PRECONDITION,
    KUI_SCI_ASYNC_MODULE_RESET_ASSERT_FAILED,
    KUI_SCI_ASYNC_MODULE_RESET_RESUME_FAILED,
    /* Signature failures OR this marker with the mismatching registers. */
    KUI_SCI_ASYNC_MODULE_RESET_SIGNATURE = 0x100,
    KUI_SCI_ASYNC_MODULE_RESET_BAD_SCR = 1,
    KUI_SCI_ASYNC_MODULE_RESET_BAD_SMR = 2,
    KUI_SCI_ASYNC_MODULE_RESET_BAD_BRR = 4,
    KUI_SCI_ASYNC_MODULE_RESET_BAD_SCMR = 8,
    KUI_SCI_ASYNC_MODULE_RESET_BAD_SSR = 16,
    KUI_SCI_ASYNC_MODULE_RESET_BAD_SPTR = 32
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
    uint32_t module_reset_attempts, module_resets, module_reset_failures;
    uint32_t module_reset_state, module_stb_before, module_stb_stopped, module_stb_after;
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
    /* Receive overruns before the payload completed. Each one whose stopped
     * channel was proven idle is retried with a fresh CMD17, at most
     * KUI_SCI_ASYNC_OVERRUN_RETRIES times per request; one that cannot be
     * proven idle still quarantines the channel (undrained_overruns). */
    uint32_t payload_overruns, overrun_retries, undrained_overruns;
    /* Card access time: 0xff bytes clocked between R1 and the data token. */
    uint32_t token_bytes, max_token_bytes;
    /* Summed time from each attempt's start to its DMA start (command and
     * card access time), and time spent in finish (checks and handoff). */
    uint64_t framing_us, finish_us;
};

/* First failed request's exceptional-stop evidence, before SCR/CHCR writes.
 * These are ordered register reads, not one simultaneous hardware snapshot.
 * Successful completion normally does not capture this record; valid==0 is
 * distinct from a captured zero value. CRC-only failures after normal
 * completion, or ownership changes first detected after stopping, can lack
 * this pre-stop record. event==0 denotes a foreground stop.
 * Context PC/SR are present only when context_valid!=0. They identify the
 * interrupted instruction, not necessarily the cause of the receive error.
 * request_elapsed_us is measured at stop entry, from begin(), including
 * command framing and any time the worker was preempted. */
struct kui_sci_async_fault {
    uint32_t valid, event, ssr, scr, dmaor, sar, dar, tcr, chcr;
    uint32_t lba, start_address, context_valid, pc, sr;
    uint64_t request_elapsed_us;
};

struct kui_sci_async_probe_result {
    enum kui_sci_async_status status;
    enum kui_sci_async_status operation_status; /* Before cleanup classification. */
    uint32_t lba;
    struct kui_sci_async_stage slow, fast;
    struct kui_sci_async_fault fault;
    /* Pre-stop evidence of the first payload overrun, even when its retry
     * succeeded. fault above still describes only a failed request. */
    struct kui_sci_async_fault first_overrun;
    uint64_t elapsed_us, max_irq_masked_us, max_irq_handler_us;
    /* Foreground API duration, including IRQ preemption; not wire time.
     * No API waits for an in-flight DMA to complete. */
    uint64_t max_open_us, max_begin_us, max_poll_us, max_finish_us;
    uint64_t max_cancel_us, max_close_us, max_call_us;
    bool started, safe_restored, guards_ok, crc_ok, baseline_ok;
    bool baseline_checked;
    bool handlers_restored, registers_restored;
    bool dma_quarantined, foreign_dma;
    /* This probe measures work during DMA and its own IRQ delivery only. */
    bool timer_irq_instrumented;
};

/* One serialized runtime reader, backed by a persistent DMA destination.
 * Initialize the handle to zero. The card and result must outlive close().
 * Callers retain exclusive ownership of the existing SCI storage session and
 * channel lease, and must not use ordinary storage until close completes.
 * Requires ready/fast card, interrupts enabled, an idle channel 1 and an
 * already-enabled DMAC interrupt priority. Both SCI GPIO directions must be
 * inputs because sampled pin reads cannot preserve foreign output latches.
 * Does not change global DMAOR or
 * the shared DMAC priority. Each poll performs at most eight framing bytes
 * (see set_framing_quantum), or a single DMA-state observation; it never
 * waits for DMA completion.
 * begin returns OK for an accepted request. poll returns PENDING until finish
 * is available, OK when ready, or a terminal error. finish copies exactly
 * 512 bytes only after guards, CRC, optional expected data, and handoff pass.
 * expected==NULL means baseline_checked=false, not a verified baseline.
 * cancel requests discard; active DMA drains through poll's bounded deadline.
 * finish after cancellation validates/hands off but never publishes data.
 * close returns PENDING while a request runs; it never silently aborts DMA.
 * Once ready, close can validate/discard the payload and release the lease.
 * safe_restored describes local hardware/IRQ cleanup, not card recovery;
 * callers still perform an ordinary verified read before trusting recovery.
 * An incomplete DMA has no documented abort-drain acknowledgement. A lone
 * receive overrun is retried only after the stopped channel's count and
 * address agree and stay unchanged across repeated checks; any late byte can
 * only land in the static receive area, which the retry rewrites and CRC and
 * guards still gate. Every other incomplete DMA, and an overrun that cannot be
 * proven idle, quarantines the destination and channel until restart.
 * A stale/copied handle cannot operate another lease. No game integration is
 * implied by this runtime API. Foreground calls are not reentrant. */
struct kui_sci_async_reader { uint32_t generation; };
#define KUI_SCI_ASYNC_OVERRUN_RETRIES 3u
enum kui_sci_async_status kui_sci_async_open(struct kui_sci_async_reader *reader,
    const struct kui_loader_sd *card, struct kui_sci_async_probe_result *out);
enum kui_sci_async_status kui_sci_async_begin(struct kui_sci_async_reader *reader,
    uint32_t lba, bool slow);
enum kui_sci_async_status kui_sci_async_poll(struct kui_sci_async_reader *reader);
enum kui_sci_async_status kui_sci_async_finish(struct kui_sci_async_reader *reader,
    uint8_t dst[512], const uint8_t expected[512]);
enum kui_sci_async_status kui_sci_async_cancel(struct kui_sci_async_reader *reader);
enum kui_sci_async_status kui_sci_async_close(struct kui_sci_async_reader *reader);
/* Framing byte operations allowed per poll (default 8). A client that does
 * not need to yield between bytes may raise it, up to 4096, so one poll can
 * send the command and wait for the token. Values are clamped to 8..4096. */
void kui_sci_async_set_framing_quantum(struct kui_sci_async_reader *reader, unsigned bytes);
/* Surround actual caller CPU work. work_sample is read-only and IRQ-safe.
 * Zero sample means no actively owned DMA. Credit
 * requires count progress with bytes still outstanding after the work. */
uint32_t kui_sci_async_work_sample(struct kui_sci_async_reader *reader);
void kui_sci_async_work_record(struct kui_sci_async_reader *reader,
    uint32_t before, uint32_t iterations, uint32_t checksum);

/* Original 16-slow/64-fast diagnostic client of the same reader API. */
enum kui_sci_async_status kui_sci_async_probe_run(
    const struct kui_loader_sd *card, uint32_t lba,
    const uint8_t baseline[512], bool (*cancelled)(void *), void *cancel_ctx,
    struct kui_sci_async_probe_result *out);
const char *kui_sci_async_status_name(enum kui_sci_async_status status);
const char *kui_sci_async_phase_name(enum kui_sci_async_phase phase);
const char *kui_sci_async_framing_name(enum kui_sci_async_framing_step step);
#endif
