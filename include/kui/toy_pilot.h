/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TOY_PILOT_H
#define KUI_TOY_PILOT_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_TOY_PILOT_MAGIC UINT32_C(0x54595031)
#define KUI_TOY_PILOT_API 3u
#define KUI_TOY_PILOT_WORKER_BEGIN UINT32_C(0x8cfd0000)
#define KUI_TOY_PILOT_WORKER_END UINT32_C(0x8cfe0000)
#define KUI_TOY_PILOT_MAIN_LEASE_BYTES UINT32_C(0x30000)
#define KUI_TOY_PILOT_STACK_BYTES 8192u
#define KUI_TOY_PILOT_BANK_FRAMES 16384u
#define KUI_TOY_PILOT_MONO_BYTES 32768u
#define KUI_TOY_PILOT_BANK_BYTES 65536u
#define KUI_TOY_PILOT_SOUND_BYTES 131072u
#define KUI_TOY_PILOT_RAW_BYTES 2352u
#define KUI_TOY_PILOT_RAW_SECTORS 2u
#define KUI_TOY_PILOT_RESET UINT32_C(0x10000)

/* All external addresses are numerical SH addresses, so this layout is also
 * deterministic in host validators. The candidate resident supplies these
 * after the real first allocator lease and worker cache publication. */
struct kui_toy_pilot_config {
    uint32_t magic, version, bytes;
    uint32_t manifest;
    /* int read_raw(uint32_t lba, uint32_t count<=2, void *aligned_output).
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
};
_Static_assert(sizeof(struct kui_toy_pilot_config)==52u,"Toy config ABI");

/* The first 68 bytes of worker.bin. Entry addresses are callable integer SH
 * functions, not offsets. INIT copies the 52-byte config on its own stack.
 * REQUEST(command,p0,p1,p2) is pure mailbox work, safe in the masked GD path;
 * positive means accepted generation, zero refusal. SNAPSHOT returns a pointer
 * to immutable-for-the-current-call numeric telemetry in the main lease. */
struct kui_toy_pilot_exports {
    uint32_t magic, version, bytes;
    uint32_t initialize, request, service_hook, am_init_hook, shutdown_hook;
    uint32_t snapshot;
    uint32_t bss_begin, bss_end, stack_bottom, stack_top, worker_end;
    uint32_t main_lease_bytes, allstop_hook, driver_load_hook;
};
_Static_assert(sizeof(struct kui_toy_pilot_exports)==68u,"Toy exports ABI");

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
    KUI_TOY_PILOT_FAULT_BUS
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
};
_Static_assert(sizeof(struct kui_toy_pilot_snapshot)==256u,"Toy telemetry v2 ABI");

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
    struct kui_toy_pilot_bank banks[2];
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
