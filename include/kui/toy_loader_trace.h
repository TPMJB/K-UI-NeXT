/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_LOADER_TRACE_H
#define KUI_TOY_LOADER_TRACE_H
#include <stddef.h>
#include <stdint.h>
#include "kui/retail_gd.h"

#ifndef KUI_TOY_PILOT_LOADER_TRACE
#define KUI_TOY_PILOT_LOADER_TRACE 0
#endif
#if KUI_TOY_PILOT_LOADER_TRACE != 0 && KUI_TOY_PILOT_LOADER_TRACE != 1
#error Toy loader trace must be 0 or 1
#endif
#if KUI_TOY_PILOT_LOADER_TRACE && (!KUI_TOY_PILOT_PRIVATE_P2 || \
    KUI_TOY_PILOT_SHARED_SCI || KUI_TOY_PILOT_ASYNC_CDDA || \
    KUI_TOY_PILOT_SYNTHETIC_SOURCE || KUI_TOY_PILOT_NATIVE_CACHE || \
    KUI_SCI_DMA_REUSE_TDRE || KUI_TOY_PILOT_GD_FIXED_STEP != 2)
#error Toy loader trace requires retained private P2 synchronous two-sector real audio
#endif

#define KUI_TOY_LOADER_TRACE_MAGIC UINT32_C(0x4c545231) /* LTR1 */
#define KUI_TOY_LOADER_TRACE_VERSION 1u
#define KUI_TOY_LOADER_TRACE_TICK_HZ 781250u
#define KUI_TOY_LOADER_TRACE_BINS 8u
#define KUI_TOY_LOADER_TRACE_WORST 8u
#define KUI_TOY_LOADER_TRACE_HEADER_WORDS 32u
#define KUI_TOY_LOADER_TRACE_PHASE_WORDS 192u
#define KUI_TOY_LOADER_TRACE_WORDS 416u
#define KUI_TOY_LOADER_TRACE_MAX_INTERVAL UINT32_C(0x7fffffff)

/* Histogram ceilings, inclusive: 782,1563,3125,6250,12500,25000,50000
 * TMU ticks; bin7 contains larger valid intervals. At the admitted frequency
 * these are approximately 1,2,4,8,16,32,64 ms. Request-size bin ceilings are
 * 1,2,4,8,16,32,64 sectors; bin7 contains larger requests.
 * All sums/counts saturate at UINT32_MAX. No live division or formatting.
 * Times describe observations at serialized adapter entry/exit, not card DMA
 * completion or the exact assembly SR-mask interval. First-credit and command
 * completion include request submission. Completion includes failed/aborted
 * terminal commands. CHECK lag ends only at an actually consumed handle.
 */
struct kui_toy_loader_trace_metric {
    uint32_t samples, ticks_total, ticks_max, ticks_min;
    uint32_t histogram[KUI_TOY_LOADER_TRACE_BINS];
};
enum kui_toy_loader_trace_record_flags {
    KUI_TOY_LOADER_TRACE_FIRST_VALID=1u,
    KUI_TOY_LOADER_TRACE_COMPLETE_VALID=2u,
    KUI_TOY_LOADER_TRACE_ACK_VALID=4u,
    KUI_TOY_LOADER_TRACE_SUCCESS=8u,
    KUI_TOY_LOADER_TRACE_FAILED=16u,
    KUI_TOY_LOADER_TRACE_ABORTED=32u
};
/* Eight-word context for a retained worst valid DATA completion latency.
 * Missing/invalid times are UINT32_MAX; corresponding VALID flags are clear.
 * Entries are unordered. CHECK updates its request's retained slot, never
 * searches by token alone (RESET can reuse tokens). Both phases keep eight.
 */
struct kui_toy_loader_trace_record {
    uint32_t token, lba, sectors, destination;
    uint32_t first_credit_ticks, complete_ticks, acknowledge_ticks, flags;
};
struct kui_toy_loader_trace_phase {
    /* Words0..35: counts are observations, not frame-rate measurements. */
    uint32_t calls, request_calls, exec_calls, check_calls, drive_calls;
    uint32_t accepted_data, rejected_data, requested_sectors, requested_bytes;
    uint32_t delivered_sectors, delivered_bytes, progress_exec, progress_check;
    uint32_t zero_progress_exec, data_completed, data_failed, data_acknowledged;
    uint32_t data_aborted, data_reset, ownership_lost;
    uint32_t sequential, overlapping, forward_seek, backward_seek;
    uint32_t invalid_clock_calls, invalid_intervals;
    uint32_t sampled_line_wraps, sampled_fb_changes, invalid_pvr_samples;
    uint32_t pvr_geometry_changes, worst_seen, worst_valid, worst_retained;
    uint32_t worst_excluded, peak_request_sectors, peak_request_bytes;
    uint32_t request_size_histogram[8]; /* Words36..43. */
    struct kui_toy_loader_trace_metric call_body; /* Words44..55. */
    struct kui_toy_loader_trace_metric pending_service_gap; /* Words56..67. */
    struct kui_toy_loader_trace_metric request_first_credit; /* Words68..79. */
    struct kui_toy_loader_trace_metric request_complete; /* Words80..91. */
    struct kui_toy_loader_trace_metric complete_acknowledge; /* Words92..103. */
    /* Words104..111: outstanding request when this phase is frozen. */
    uint32_t outstanding_token, outstanding_lba, outstanding_sectors;
    uint32_t outstanding_destination, outstanding_credited_bytes;
    uint32_t outstanding_started_tick, outstanding_clock_valid;
    uint32_t outstanding_terminal_observed;
    /* EXEC/CHECK only, with a DATA handle owned at adapter entry. Includes
     * zero-progress and terminal CHECK visits; never scalar/audio bodies. */
    struct kui_toy_loader_trace_metric data_service_body; /* Words112..123. */
    uint32_t reserved[4]; /* Words124..127, always zero. */
    struct kui_toy_loader_trace_record worst[8]; /* Words128..191. */
};
struct kui_toy_loader_trace_report {
    /* Word0..31. Phase0 is startup until the first accepted PLAY mailbox;
     * phase1 starts afterwards. That boundary is not an audible-start proof.
     * sampled_* counters can miss wraps/changes between loader observations.
     */
    uint32_t magic, version, words, phase_words, record_words, worst_per_phase;
    uint32_t tick_hz, active_phase, frozen, accepted_play_requests;
    uint32_t boundary_command, boundary_token, boundary_generation;
    uint32_t boundary_tick, boundary_clock_valid;
    uint32_t first_tick, first_clock_valid, counters_saturated;
    uint32_t clock_epoch, unmatched_ends, nested_begins, total_call_samples;
    uint32_t reserved[10];
    struct kui_toy_loader_trace_phase phase[2];
};
_Static_assert(sizeof(struct kui_toy_loader_trace_metric)==12u*4u,"loader metric ABI");
_Static_assert(sizeof(struct kui_toy_loader_trace_record)==8u*4u,"loader context ABI");
_Static_assert(offsetof(struct kui_toy_loader_trace_phase,request_size_histogram)==36u*4u,"loader sizes ABI");
_Static_assert(offsetof(struct kui_toy_loader_trace_phase,call_body)==44u*4u,"loader timing ABI");
_Static_assert(offsetof(struct kui_toy_loader_trace_phase,worst)==128u*4u,"loader contexts ABI");
_Static_assert(sizeof(struct kui_toy_loader_trace_phase)==192u*4u,"loader phase ABI");
_Static_assert(offsetof(struct kui_toy_loader_trace_report,phase)==32u*4u,"loader report phases ABI");
_Static_assert(sizeof(struct kui_toy_loader_trace_report)==416u*4u,"loader report ABI");

/* Serialized GD dispatch only. These never map/modify guest memory, call
 * native SDK functions, perform I/O, acknowledge handles, or alter service.
 */
void kui_toy_loader_trace_begin(const struct kui_retail_gd *,uint32_t function,
    uint32_t r4,uint32_t r5);
void kui_toy_loader_trace_end(const struct kui_retail_gd *,uint32_t function,
    uint32_t r4,uint32_t r5,int32_t result);
const uint32_t *kui_toy_loader_trace_words(void);
uint32_t kui_toy_loader_trace_word_count(void);
void kui_toy_loader_trace_freeze(void);

#ifdef KUI_TOY_LOADER_TRACE_HOST_TEST
/* Only host builds replace read-only MMIO and expose fresh-process reset. */
uint32_t kui_toy_loader_trace_host_read(uint32_t address,unsigned bytes);
void kui_toy_loader_trace_host_reset(void);
#endif
#endif
