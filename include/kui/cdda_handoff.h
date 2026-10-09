/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_HANDOFF_H
#define KUI_CDDA_HANDOFF_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "kui/cdda_clock.h"

/* A distinct controlled-homebrew contract, never a KUIRUN header extension.
 * Wire words are little-endian uint32_t; there are no native pointers here. */
#define KUI_CDDA_HANDOFF_MAGIC0 0x4444434bu /* "KCDD" */
#define KUI_CDDA_HANDOFF_MAGIC1 0x00314841u /* "AH1\0" */
#define KUI_CDDA_HANDOFF_VERSION 1u
#define KUI_CDDA_HANDOFF_BYTES 160u
#define KUI_CDDA_HANDOFF_SERVICE_REVISION 1u
#define KUI_CDDA_HANDOFF_COOPERATIVE 1u
#define KUI_CDDA_HANDOFF_CONTROLLED 2u
#define KUI_CDDA_HANDOFF_FLAGS 3u
#define KUI_CDDA_HANDOFF_SCI 1u
#define KUI_CDDA_HANDOFF_READ_ONLY 1u
#define KUI_CDDA_HANDOFF_SERIAL_OWNER 2u
#define KUI_CDDA_HANDOFF_STORAGE_RIGHTS 3u
#define KUI_CDDA_HANDOFF_OWN_SOUND 1u
#define KUI_CDDA_HANDOFF_OWN_CHANNELS 2u
#define KUI_CDDA_HANDOFF_OWN_TMU 4u
#define KUI_CDDA_HANDOFF_OWN_ENGINE_MEMORY 8u
#define KUI_CDDA_HANDOFF_PRIVATE_CLIENT_MEMORY 16u
#define KUI_CDDA_HANDOFF_RESOURCE_RIGHTS 31u
#define KUI_CDDA_HANDOFF_RAM_FIRST 0x8c010000u
#define KUI_CDDA_HANDOFF_RAM_END 0x8d000000u
#define KUI_CDDA_HANDOFF_SOUND_FIRST 0x00100000u
#define KUI_CDDA_HANDOFF_SOUND_END 0x00110000u
/* Floor190 ms. Actual audio-ring policy may require a smaller limit. */
#define KUI_CDDA_HANDOFF_MAX_GAP_TICKS 2369056u

struct kui_cdda_handoff_desc {
    uint32_t magic0,magic1,version,bytes,service_revision;
    uint32_t flags,storage_transport,storage_rights,resource_rights,tmu_unit;
    uint32_t sound_channels,clock_hz,service_gap_ticks;
    uint32_t engine_code_first,engine_code_end,engine_state_first,engine_state_end;
    uint32_t engine_stack_first,engine_stack_end,service_stack_first,service_stack_end;
    uint32_t client_code_first,client_code_end,client_stack_first,client_stack_end;
    uint32_t sound_first,sound_end,client_entry,service_entry;
    uint32_t reserved[11];
};
enum kui_cdda_handoff_result {
    KUI_CDDA_HANDOFF_OK,KUI_CDDA_HANDOFF_INVALID,KUI_CDDA_HANDOFF_REVISION,
    KUI_CDDA_HANDOFF_RIGHTS,KUI_CDDA_HANDOFF_RANGE,KUI_CDDA_HANDOFF_OVERLAP,
    KUI_CDDA_HANDOFF_ENTRY
};
/* Canonical cached P1 main RAM only: physical/P2 aliases are rejected, never
 * masked. All six code/state/stack ranges are nonempty,32-byte aligned and
 * pairwise disjoint, including the dedicated service stack. Entries are
 * SH4 instruction aligned and covered by the appropriate code range.
 * Sound bounds, channels0/1,TMU1,SCI/read-only ownership are exact for v1. */
enum kui_cdda_handoff_result kui_cdda_handoff_validate(const struct kui_cdda_handoff_desc *);
/* Decode exactly160 LE bytes and validate them before exposing any output.
 * The output is unchanged on failure; no unaligned native struct reads. */
enum kui_cdda_handoff_result kui_cdda_handoff_decode(const uint8_t *,size_t,
    struct kui_cdda_handoff_desc *);

/* Native exports/context pointers belong in a separate platform adapter; they
 * must never be written into, or reconstructed by masking, this wire struct. */
enum kui_cdda_service_state {
    KUI_CDDA_SERVICE_IDLE,KUI_CDDA_SERVICE_ACTIVE,KUI_CDDA_SERVICE_BUSY,
    KUI_CDDA_SERVICE_STOPPED,KUI_CDDA_SERVICE_FAULT
};
enum kui_cdda_service_result {
    KUI_CDDA_SERVICE_OK,KUI_CDDA_SERVICE_INVALID,KUI_CDDA_SERVICE_STATE,
    KUI_CDDA_SERVICE_BUSY_RESULT,KUI_CDDA_SERVICE_STALE,KUI_CDDA_SERVICE_DEADLINE,
    KUI_CDDA_SERVICE_IO,KUI_CDDA_SERVICE_OVERFLOW
};
struct kui_cdda_service_ticket {uint32_t epoch,call;};
struct kui_cdda_service_guard {
    enum kui_cdda_service_state state;
    uint32_t generation,call_generation,last_tick,gap_ticks;
    struct kui_cdda_service_ticket pending;
    bool initialized;
};
/* Initialize once with no physical service in flight; old callbacks must have
 * been discarded before reinitialization. Generations never wrap thereafter. */
enum kui_cdda_service_result kui_cdda_service_init(struct kui_cdda_service_guard *);
/* Owner-only start/reset after PREVIOUS owned audio has stopped and previous
 * physical work has quiesced. Fresh audio may already be primed/keyed on;
 * now anchors its service lease at that new playback start, so initial prefill
 * does not consume the ongoing lease. Only IDLE/STOPPED/FAULT admit a fresh
 * generation. The output epoch cannot alias guard state and is unchanged on
 * refusal. */
enum kui_cdda_service_result kui_cdda_service_start(struct kui_cdda_service_guard *,
    uint32_t now,uint32_t gap_ticks,uint32_t *epoch);
/* Mandatory pure stale/busy checks BEFORE platform phase queries, reads or
 * writes. Only OK grants permission to do service work. Unsigned tick gaps
 * must be less than the limit and less than one full32-bit counter period;
 * a backwards clock therefore refuses. Exact-limit gaps already fault. */
enum kui_cdda_service_result kui_cdda_service_enter(struct kui_cdda_service_guard *,
    uint32_t epoch,uint32_t now,struct kui_cdda_service_ticket *);
/* Complete the exact currently admitted call before the same gap expires from
 * the previous successful completion/start; entering does not extend a lease.
 * IO/DEADLINE clear the pending
 * token and enter FAULT; the platform must stop its owned audio. A service
 * must itself observe its ring deadline before publishing after blocking I/O:
 * this guard cannot undo audio already heard during an arbitrary stall. */
enum kui_cdda_service_result kui_cdda_service_leave(struct kui_cdda_service_guard *,
    const struct kui_cdda_service_ticket *,uint32_t now,bool success);
/* Matching owner stop is idempotent when stopped. BUSY refuses, so a nested
 * client cannot unlock or reset its active synchronous service. */
enum kui_cdda_service_result kui_cdda_service_stop(struct kui_cdda_service_guard *,uint32_t epoch);
#endif
