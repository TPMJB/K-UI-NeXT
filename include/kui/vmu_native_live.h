/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_LIVE_H
#define KUI_VMU_NATIVE_LIVE_H
#include "kui/vmu_native_layout.h"

/* A launch-local proof of immutable geometry, constructed in the worker's
 * final private BSS. Never serialize, relocate, or mutate a prepared object.
 * Preparation runs the full reference layout/runtime validators. Subsequent
 * checks retain every live pointer, RX slot, callback and MMIO witness under
 * IMASK15, without recomputing static span/provenance intersections. Heap,
 * installed hooks and exact TX/payload ownership are independent guards.
 * The original full validators must still surround external storage loans. */
struct kui_vmu_native_live {
    uintptr_t self;
    uint32_t ready,manager_cell,manager,lock_cell,lock,tx,rx,completion_return;
    uint32_t lease_begin,lease_bytes,guard_begin,guard_bytes;
    uint32_t gun,timer1,timer2,input_count,input_address[24],input_value[24];
    uint32_t rx_slots[KUI_VMU_NATIVE_RX_SLOTS],nonzero;
};
/* Runtime and snapshot are modified only after both full validators pass.
 * Out must be separate final private storage, disjoint from the inputs. */
enum kui_vmu_native_runtime_result kui_vmu_native_live_prepare(
    struct kui_vmu_native_live *out,const struct kui_vmu_native_layout *,
    const struct kui_vmu_native_discovery *,struct kui_vmu_native_runtime_config *,
    const struct kui_vmu_native_runtime_boundary *,kui_vmu_native_runtime_read,
    void *,struct kui_vmu_native_runtime_snapshot *,
    struct kui_vmu_native_layout_diagnostic *);
enum kui_vmu_native_runtime_result kui_vmu_native_live_check(
    const struct kui_vmu_native_live *,const struct kui_vmu_native_discovery *,
    const struct kui_vmu_native_runtime_boundary *,
    kui_vmu_native_runtime_read,void *,struct kui_vmu_native_runtime_snapshot *);
#endif
