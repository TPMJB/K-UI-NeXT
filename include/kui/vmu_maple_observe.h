/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_MAPLE_OBSERVE_H
#define KUI_VMU_MAPLE_OBSERVE_H

#define KUI_VMU_MAPLE_OBSERVE_MAGIC 0x564d4f31
#define KUI_VMU_MAPLE_OBSERVE_VERSION 4
#ifndef KUI_VMU_READONLY_PROOF
#define KUI_VMU_READONLY_PROOF 0
#endif
#if KUI_VMU_READONLY_PROOF != 0 && KUI_VMU_READONLY_PROOF != 1
#error VMU proof mode must be explicitly zero or one
#endif
#define KUI_VMU_MAPLE_OBSERVE_SITE 0x8c0bf264
#define KUI_VMU_MAPLE_OBSERVE_RESUME 0x8c0bf270
#define KUI_VMU_MAPLE_OBSERVE_LEASE 0x8cfd0000
#define KUI_VMU_MAPLE_OBSERVE_LIMIT 0x8cfe0000
#define KUI_VMU_MAPLE_OBSERVE_LEASE_BYTES 0x30000
#define KUI_VMU_MAPLE_OBSERVE_STACK_BYTES 4096
#define KUI_VMU_MAPLE_OBSERVE_EXPORT_BYTES 60
#define KUI_VMU_MAPLE_OBSERVE_CONFIG_BYTES 76
#define KUI_VMU_MAPLE_OBSERVE_SNAPSHOT_BYTES 612
#define KUI_VMU_MAPLE_OBSERVE_CONSUMER 0x8c0bf37a
#define KUI_VMU_MAPLE_OBSERVE_COMPLETION_SLOT 0x8c0b540c
#define KUI_VMU_MAPLE_OBSERVE_COMPLETION_PR 0x8c0b5362
#define KUI_VMU_MAPLE_OBSERVE_DEFAULT_STOP 18000
#define KUI_VMU_MAPLE_OBSERVE_DEFAULT_POST 1200
#define KUI_VMU_MAPLE_OBSERVE_MAX_STARTS 18000
#define KUI_VMU_MAPLE_OBSERVE_RECORDS 4

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

struct kui_vmu_maple_observe_exports {
    uint32_t magic, version, bytes;
    uint32_t initialize, hook, bss_begin, bss_end;
    uint32_t stack_bottom, stack_top, worker_end, lease_bytes, snapshot, terminal, complete_hook;
    uint32_t readonly_mode;
};

struct kui_vmu_maple_observe_config {
    uint32_t magic, version, bytes, stop_after, post_after;
    uint32_t display[14];
};

enum kui_vmu_maple_observe_stop {
    KUI_VMU_OBSERVE_RUNNING, KUI_VMU_OBSERVE_UNSAFE,
    KUI_VMU_OBSERVE_HARD_LIMIT, KUI_VMU_OBSERVE_CARD_WINDOW,
    KUI_VMU_OBSERVE_STACK_FAULT, KUI_VMU_OBSERVE_OWNER_TERMINAL,
    KUI_VMU_OBSERVE_PROOF_REFUSED
};

struct kui_vmu_maple_observe_record {
    uint32_t ordinal, header, payload0, payload1, rx;
};

struct kui_vmu_maple_observe_snapshot {
    uint32_t magic, version, bytes, initialized;
    uint32_t starts, descriptors, malformed, unsupported;
    uint32_t source_min, source_max, source_last, source_changes;
    uint32_t descriptors_max, stop_after, stack_fault, incomplete;
    uint32_t ports[4];
    uint32_t units[6]; /* Main peripheral, followed by subunit 1..5. */
    uint32_t commands[16];
    uint32_t source_samples[8];
    uint32_t rx_min, rx_max, rx_bad, controller_seen;
    uint32_t source_fingerprint, last_control, last_rx, last_header;
    uint32_t trigger_last, trigger_other, enable_last, enable_other;
    uint32_t busy_last, busy_active, entry_sr_last, entry_masks;
    uint32_t manager_last, manager_other;
    uint32_t first_subunit_start, first_card_start, post_after, subunit_requests, stop_reason;
    struct kui_vmu_maple_observe_record first[KUI_VMU_MAPLE_OBSERVE_RECORDS];
    struct kui_vmu_maple_observe_record last[KUI_VMU_MAPLE_OBSERVE_RECORDS];
    uint32_t completion_calls, completion_matched, completion_orphan, completion_overwritten;
    uint32_t completion_pending, completion_busy, completion_bad_mask, completion_rx_headers;
    uint32_t completion_sr_last, completion_masks, main_a_response, main_a_rx;
    uint32_t subunit_request, subunit_response, subunit_rx, subunit_ordinal;
    uint32_t completion_bad_context, completion_guard_flags, completion_source, completion_pr_last;
    uint32_t fault_cursor, nop_words;
    uint32_t proof_mode, proof_prepared, proof_completed, proof_parents;
    uint32_t proof_replies, proof_info, proof_media, proof_reads;
    uint32_t proof_conflicts, proof_refused, proof_bank_fail, proof_flags;
    uint32_t proof_last_request, proof_last_response, proof_last_rx, proof_last_block;
    uint32_t proof_bytes, proof_commit_attempts;
};

_Static_assert(sizeof(struct kui_vmu_maple_observe_exports) ==
    KUI_VMU_MAPLE_OBSERVE_EXPORT_BYTES, "Maple observer exports ABI");
_Static_assert(sizeof(struct kui_vmu_maple_observe_config) ==
    KUI_VMU_MAPLE_OBSERVE_CONFIG_BYTES, "Maple observer config ABI");
_Static_assert(sizeof(struct kui_vmu_maple_observe_snapshot) ==
    KUI_VMU_MAPLE_OBSERVE_SNAPSHOT_BYTES, "Maple observer snapshot ABI");
_Static_assert(sizeof(struct kui_vmu_maple_observe_record) == 20,
    "Maple observer metadata record ABI");

int kui_vmu_maple_observer_init(const struct kui_vmu_maple_observe_config *);
void kui_vmu_maple_observer_visit(uint32_t inherited_sr);
void kui_vmu_maple_observer_complete(uint32_t inherited_sr,uint32_t inherited_pr);
void kui_vmu_maple_observer_hook(void);
void kui_vmu_maple_observer_complete_hook(void);
void kui_vmu_maple_observer_terminal(void) __attribute__((noreturn));
extern struct kui_vmu_maple_observe_snapshot kui_vmu_maple_observer_snapshot;

#ifdef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
uint32_t kui_vmu_maple_observer_test_read(uint32_t address);
void kui_vmu_maple_observer_test_stop(void);
#if KUI_VMU_READONLY_PROOF
uint8_t *kui_vmu_maple_observer_test_response(uint32_t physical);
#endif
#endif
#endif
#endif
