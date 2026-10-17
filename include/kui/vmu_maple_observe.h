/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_MAPLE_OBSERVE_H
#define KUI_VMU_MAPLE_OBSERVE_H

#define KUI_VMU_MAPLE_OBSERVE_MAGIC 0x564d4f31
#define KUI_VMU_MAPLE_OBSERVE_VERSION 1
#define KUI_VMU_MAPLE_OBSERVE_SITE 0x8c0bf264
#define KUI_VMU_MAPLE_OBSERVE_RESUME 0x8c0bf270
#define KUI_VMU_MAPLE_OBSERVE_LEASE 0x8cfd0000
#define KUI_VMU_MAPLE_OBSERVE_LIMIT 0x8cfe0000
#define KUI_VMU_MAPLE_OBSERVE_LEASE_BYTES 0x30000
#define KUI_VMU_MAPLE_OBSERVE_STACK_BYTES 4096
#define KUI_VMU_MAPLE_OBSERVE_EXPORT_BYTES 52
#define KUI_VMU_MAPLE_OBSERVE_CONFIG_BYTES 72
#define KUI_VMU_MAPLE_OBSERVE_SNAPSHOT_BYTES 272
#define KUI_VMU_MAPLE_OBSERVE_DEFAULT_STOP 1200

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

struct kui_vmu_maple_observe_exports {
    uint32_t magic, version, bytes;
    uint32_t initialize, hook, bss_begin, bss_end;
    uint32_t stack_bottom, stack_top, worker_end, lease_bytes, snapshot, terminal;
};

struct kui_vmu_maple_observe_config {
    uint32_t magic, version, bytes, stop_after;
    uint32_t display[14];
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
};

_Static_assert(sizeof(struct kui_vmu_maple_observe_exports) ==
    KUI_VMU_MAPLE_OBSERVE_EXPORT_BYTES, "Maple observer exports ABI");
_Static_assert(sizeof(struct kui_vmu_maple_observe_config) ==
    KUI_VMU_MAPLE_OBSERVE_CONFIG_BYTES, "Maple observer config ABI");
_Static_assert(sizeof(struct kui_vmu_maple_observe_snapshot) ==
    KUI_VMU_MAPLE_OBSERVE_SNAPSHOT_BYTES, "Maple observer snapshot ABI");

int kui_vmu_maple_observer_init(const struct kui_vmu_maple_observe_config *);
void kui_vmu_maple_observer_visit(uint32_t inherited_sr);
void kui_vmu_maple_observer_hook(void);
void kui_vmu_maple_observer_terminal(void) __attribute__((noreturn));
extern struct kui_vmu_maple_observe_snapshot kui_vmu_maple_observer_snapshot;

#ifdef KUI_VMU_MAPLE_OBSERVE_HOST_TEST
uint32_t kui_vmu_maple_observer_test_read(uint32_t address);
void kui_vmu_maple_observer_test_stop(void);
#endif
#endif
#endif
