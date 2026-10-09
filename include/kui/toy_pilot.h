/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_H
#define KUI_TOY_PILOT_H
#include <stdbool.h>
#include <stdint.h>
#include "kui/toy_pilot_cache.h"

/* Diagnostic control: remove only audio card reads, retaining the native
 * AICA playback/timeline and DATA exclusion. This is deliberately silent. */
#ifndef KUI_TOY_PILOT_SYNTHETIC_SOURCE
#define KUI_TOY_PILOT_SYNTHETIC_SOURCE 0
#endif
#if KUI_TOY_PILOT_SYNTHETIC_SOURCE != 0 && KUI_TOY_PILOT_SYNTHETIC_SOURCE != 1
#error Toy synthetic source must be 0 or 1
#endif
#if KUI_TOY_PILOT_SYNTHETIC_SOURCE && (KUI_TOY_PILOT_SHARED_SCI || KUI_TOY_PILOT_ASYNC_CDDA)
#error Toy synthetic source requires the retained synchronous SCI profile
#endif

#define KUI_TOY_PILOT_MAGIC UINT32_C(0x54595031)
#define KUI_TOY_PILOT_API 8u
#define KUI_TOY_PILOT_WORKER_BEGIN UINT32_C(0x8cfd0000)
#define KUI_TOY_PILOT_WORKER_END UINT32_C(0x8cfe0000)
#define KUI_TOY_PILOT_MAIN_LEASE_BYTES UINT32_C(0x30000)
#define KUI_TOY_PILOT_STACK_BYTES 8192u
#define KUI_TOY_PILOT_BLOCKS 8u
#define KUI_TOY_PILOT_BLOCK_FRAMES 4096u
#define KUI_TOY_PILOT_BANK_FRAMES KUI_TOY_PILOT_BLOCK_FRAMES
#define KUI_TOY_PILOT_MONO_BYTES 8192u
#define KUI_TOY_PILOT_BANK_BYTES 16384u
#define KUI_TOY_PILOT_RING_MONO_BYTES 65536u
#define KUI_TOY_PILOT_SOUND_BYTES 131072u
#define KUI_TOY_PILOT_RAW_BYTES 2352u
#define KUI_TOY_PILOT_RAW_SECTORS 1u
#define KUI_TOY_PILOT_RESET UINT32_C(0x10000)

/* All external addresses are numerical SH addresses, so this layout is also
 * deterministic in host validators. The candidate resident supplies these
 * after the real first allocator lease and worker cache publication. */
struct kui_toy_pilot_config {
    uint32_t magic, version, bytes;
    uint32_t manifest;
    /* int read_raw(uint32_t lba, uint32_t count==1, void *aligned_output).
     * The callback masks/restores exact SR, claims the resident lock and
     * closes the card stream and releases the bus on EVERY return. */
    uint32_t read_raw;
    uint32_t resident_active;
    /* Address of native GD command word: only command16/17 blocks raw
     * audio filling. Audio command20 must not block its own worker. */
    uint32_t data_pending;
    uint32_t main_lease_begin, main_lease_end;
    uint32_t worker_begin, worker_end;
    uint32_t code_bytes;
    /* Exact linked low-resident terminal hook. A latched worker fault enters
     * its numeric report after restoring the caller SR; no game teardown. */
    uint32_t terminal_entry;
#if KUI_TOY_PILOT_SHARED_SCI
    /* Exact low singleton: the high receiver shares its bus callbacks. */
    uint32_t sci_card, sci_acquire, sci_release, sci_healthy;
#endif
};
#if KUI_TOY_PILOT_SHARED_SCI
_Static_assert(sizeof(struct kui_toy_pilot_config)==68u,"Toy shared config ABI");
#else
_Static_assert(sizeof(struct kui_toy_pilot_config)==52u,"Toy config ABI");
#endif

/* The first 76 bytes of worker.bin. Entry addresses are callable integer SH
 * functions, not offsets. INIT copies the sized config on its own stack.
 * REQUEST(command,p0,p1,p2) is pure mailbox work, safe in the masked GD path;
 * positive means accepted generation, zero refusal. SNAPSHOT returns a pointer
 * to immutable-for-the-current-call numeric telemetry in the main lease. */
struct kui_toy_pilot_exports {
    uint32_t magic, version, bytes;
    uint32_t initialize, request, service_hook, am_init_hook, shutdown_hook;
    uint32_t snapshot;
    uint32_t bss_begin, bss_end, stack_bottom, stack_top, worker_end;
    uint32_t main_lease_bytes, allstop_hook, driver_load_hook;
    uint32_t gd_dispatch, pause_hook;
#if KUI_TOY_PILOT_LOADER_TRACE
    uint32_t trace_terminal;
#endif
};
#if KUI_TOY_PILOT_LOADER_TRACE
_Static_assert(sizeof(struct kui_toy_pilot_exports)==80u,"Toy trace exports ABI");
#else
_Static_assert(sizeof(struct kui_toy_pilot_exports)==76u,"Toy exports ABI");
#endif

enum kui_toy_pilot_state {
    KUI_TOY_PILOT_OFF, KUI_TOY_PILOT_STOPPED, KUI_TOY_PILOT_PREFILL,
    KUI_TOY_PILOT_START_WAIT, KUI_TOY_PILOT_PLAYING,
    KUI_TOY_PILOT_PAUSED, KUI_TOY_PILOT_EOF, KUI_TOY_PILOT_FAULT
};
enum kui_toy_pilot_fault {
    KUI_TOY_PILOT_FAULT_NONE, KUI_TOY_PILOT_FAULT_CONFIG,
    KUI_TOY_PILOT_FAULT_DRIVER, KUI_TOY_PILOT_FAULT_HEAP,
    KUI_TOY_PILOT_FAULT_PORT, KUI_TOY_PILOT_FAULT_QUEUE,
    KUI_TOY_PILOT_FAULT_CARD, KUI_TOY_PILOT_FAULT_RANGE,
    KUI_TOY_PILOT_FAULT_CLOCK, KUI_TOY_PILOT_FAULT_STACK,
    KUI_TOY_PILOT_FAULT_GENERATION, KUI_TOY_PILOT_FAULT_PHASE,
    KUI_TOY_PILOT_FAULT_BUS, KUI_TOY_PILOT_FAULT_NATIVE_CONTROL
};
/* Counts describe successfully queued internal STOP/reprime decisions, not
 * intentional control operations, source EOF or failed STOP attempts. */
enum kui_toy_pilot_recovery {
    KUI_TOY_PILOT_RECOVERY_PORTS=1,
    KUI_TOY_PILOT_RECOVERY_PHASE,
    KUI_TOY_PILOT_RECOVERY_UNOBSERVED_HALF,
    KUI_TOY_PILOT_RECOVERY_PROGRESS,
    KUI_TOY_PILOT_RECOVERY_START_PROOF,
    KUI_TOY_PILOT_RECOVERY_MISSING_HALF,
    KUI_TOY_PILOT_RECOVERY_COPY_RESERVE,
    KUI_TOY_PILOT_RECOVERY_STREAM_OVERFLOW
};
enum kui_toy_pilot_reserve_site {
    KUI_TOY_PILOT_RESERVE_PLAYBACK=1,
    KUI_TOY_PILOT_RESERVE_FILL_BEGIN,
    KUI_TOY_PILOT_RESERVE_PRE_CARD,
    KUI_TOY_PILOT_RESERVE_POST_CARD,
    KUI_TOY_PILOT_RESERVE_COPY_PLANE
};
struct kui_toy_pilot_snapshot {
    uint32_t magic, version, bytes, state, fault;
    uint32_t generation, applied_generation, driver_generation, command, parameters[3];
    uint32_t position_fad, track, end_fad;
    uint32_t main_begin, main_end, worker_end, stack_used, stack_fault;
    uint32_t driver_bytes, driver_crc, driver_verified;
    uint32_t sound_address, sound_bytes, sound_generation;
    uint32_t service_calls, service_skips, service_gap_max, step_ticks_max;
    uint32_t raw_calls, raw_bytes, raw_errors, copy_calls, copy_ticks_max;
    uint32_t bank_fills, bank_starts, bank_ends, handoff_gaps, gap_ticks_max;
    uint32_t start_waits, stop_waits, queue_errors, stale_actions;
    uint32_t active_left, active_right, cursor_left, cursor_right;
    uint32_t started_observed, finite_ends, shutdowns, sdk_init_result;
    /* programmed/source frames retired; this is NOT an audible-frame proof */
    uint32_t retired_frames, filled_frames, queue_producer, queue_consumer;
    uint32_t dma_busy, dma_suspended, hardware_loops, active_bank_writes;
    uint32_t bus_last_result, bus_deferrals, updater_entries, updater_returns;
    uint32_t pause_entries, pause_retries, pause_pumps, pause_retired;
    uint32_t pause_detail, pause_max_attempts, cache_policy, service_visits_per_update;
    /* Captured before a native-control fault. GD CHECK remains authoritative;
     * these breadcrumbs never release a handle or alter its native owner. */
    uint32_t native_owner, native_work_token, gd_owned_command, gd_owned_token;
    uint32_t native_gd_state, check_token, check_destination, check_result;
    /* v5 appends sixteen words after the original eighty. Counts index the
     * one-based recovery enum minus one; last_reason zero means none. The
     * last GD command and accepted-cursor age are captured at the decision,
     * before its successful STOP. Cursor age is zero before start proof. */
    uint32_t recovery_counts[8];
    /* Sampled span from the first data16/17 denial while a running ring
     * needs its next ordered block, through the next observed fill opportunity,
     * control application/revocation or successful recovery. This includes
     * gaps between worker visits, not physical card-busy time. Totals wrap
     * modulo32 bits; an open span contributes to interval count and timing
     * only after it closes. */
    uint32_t data_blocked_calls, data_blocked_intervals;
    uint32_t data_blocked_ticks_max, data_blocked_ticks_total;
    uint32_t recovery_last_reason, recovery_last_gd_command;
    uint32_t recovery_last_cursor_age, data_blocked_open;
    /* v6 pressure diagnostics append sixteen words after the v5 prefix.
     * The most recent COPY_RESERVE denial is captured before its STOP
     * attempt. Ages are ticks; remaining and filled are PCM frames.
     * A capture can survive a deferred or superseded STOP, so its fields
     * are decision inputs, not another successful-recovery count. */
    uint32_t reserve_last_cursor, reserve_last_proof_age;
    uint32_t reserve_last_sample_age, reserve_last_remaining;
    uint32_t reserve_last_bank, reserve_last_bank_filled;
    uint32_t reserve_last_fill_stream, reserve_last_site;
    /* Source acquisition duration only (generated PCM in the explicit
     * SYNTHETIC_SOURCE control, actual raw callback otherwise), including
     * failed reads but not
     * cached sector remnants or callbacks returning to a revoked/stale
     * epoch. The total wraps modulo32 bits. */
    uint32_t raw_read_ticks_last, raw_read_ticks_max;
    uint32_t raw_read_ticks_total, raw_read_timing_calls;
    uint32_t reserve_last_bank_state, reserve_last_probe_age;
    /* v8 keeps the v7 wire layout but bank IDs and counters describe eight
     * 4096-frame blocks. Remaining is distance to this block's next absolute
     * start, protected by the faster channel. Window ticks and service visits
     * begin at this block's accepted slower-channel retirement (or START),
     * including that observation's visit and the denied visit. This is an
     * observed refill window, not the hardware block-entry time. Ordinary
     * proof renewals retain the anchor; unsigned arithmetic wraps
     * modulo32 bits. These measurements never grant write permission. */
    uint32_t reserve_last_window_ticks, reserve_last_window_calls;
};
_Static_assert(sizeof(struct kui_toy_pilot_snapshot)==448u,"Toy telemetry v8 ABI");

enum kui_toy_pilot_bank_state {
    KUI_TOY_PILOT_BANK_EMPTY, KUI_TOY_PILOT_BANK_FILLING,
    KUI_TOY_PILOT_BANK_READY, KUI_TOY_PILOT_BANK_START_WAIT,
    KUI_TOY_PILOT_BANK_PLAYING
};
/* Pure finite-bank ownership model. A queue acknowledgement alone never
 * authorizes playback completion. The adapter supplies actual applied-start
 * and finite-end evidence; stale epochs and premature end observations fail. */
struct kui_toy_pilot_bank {
    uint32_t state, generation, first_frame, frames, filled;
};
struct kui_toy_pilot_model {
    uint32_t generation, active_bank, pending_bank, stopped;
    struct kui_toy_pilot_bank banks[KUI_TOY_PILOT_BLOCKS];
};
void kui_toy_pilot_model_init(struct kui_toy_pilot_model *);
bool kui_toy_pilot_model_reset(struct kui_toy_pilot_model *);
bool kui_toy_pilot_model_fill_begin(struct kui_toy_pilot_model *,uint32_t bank,
    uint32_t generation,uint32_t first_frame,uint32_t frames);
bool kui_toy_pilot_model_fill_commit(struct kui_toy_pilot_model *,uint32_t bank,
    uint32_t generation,uint32_t frames);
bool kui_toy_pilot_model_start_queued(struct kui_toy_pilot_model *,uint32_t bank,
    uint32_t generation);
bool kui_toy_pilot_model_start_applied(struct kui_toy_pilot_model *,uint32_t bank,
    uint32_t generation,bool left_active,bool right_active);
bool kui_toy_pilot_model_finite_end(struct kui_toy_pilot_model *,uint32_t bank,
    uint32_t generation,bool left_inactive,bool right_inactive,bool applied_end);
bool kui_toy_pilot_model_stop_applied(struct kui_toy_pilot_model *,
    uint32_t generation,bool commands_consumed,bool left_inactive,bool right_inactive);

#endif
