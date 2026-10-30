/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_PASSIVE_H
#define KUI_VMU_NATIVE_PASSIVE_H
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_runtime.h"
#include "kui/vmu_native_layout.h"
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#include "kui/vmu_durable.h"
#include "kui/vmu_native_exit.h"
#endif
#define KUI_VMU_NATIVE_PASSIVE_MAGIC UINT32_C(0x4b4e5031)
/* Canonical selector in the existing config word; no exports/config ABI growth. */
enum kui_vmu_native_diagnostics {
    KUI_VMU_NATIVE_DIAGNOSTICS_NORMAL = 0,
    KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE = 1,
    KUI_VMU_NATIVE_DIAGNOSTICS_HOOKS_ONLY = 2,
    KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN = 3,
    KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE = 4,
    KUI_VMU_NATIVE_DIAGNOSTICS_READER_ONLY = 5
};
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
#define KUI_VMU_NATIVE_PASSIVE_VERSION 4u
#if KUI_NATIVE_GENERAL_LEAN_LAYOUT
#define KUI_VMU_NATIVE_PASSIVE_SPAN 0x1c000u
#else
#define KUI_VMU_NATIVE_PASSIVE_SPAN 0x20000u
#endif
#else
#define KUI_VMU_NATIVE_PASSIVE_VERSION 1u
#define KUI_VMU_NATIVE_PASSIVE_SPAN 0x10000u
#endif
struct kui_vmu_native_passive_exports {
    uint32_t magic,version,bytes,arm,submit_hook,complete_hook,terminal;
    uint32_t bss_begin,bss_end,stack_bottom,stack_top,memory_end;
    uint32_t resume_cell,consumer_cell;
#if KUI_VMU_NATIVE_READONLY_PROBE
    uint32_t release_hook,release_resume_cell;
#else
    uint32_t reserved[2];
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    uint32_t sd_transaction,runtime_fault_hook,allocation_failure_hook,trace_exports;
#endif
};
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
_Static_assert(sizeof(struct kui_vmu_native_passive_exports)==80u,"Persistent worker exports ABI");
#else
_Static_assert(sizeof(struct kui_vmu_native_passive_exports)==64u,"Passive worker exports ABI");
#endif
struct kui_vmu_native_passive_config {
    uint32_t version,bytes,tx_base;
    struct kui_vmu_native_discovery driver;
    struct kui_vmu_native_heap_lease lease;
    struct kui_vmu_native_layout layout;
    struct kui_vmu_native_runtime_config runtime;
    uint32_t display[14];
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    struct kui_vmu_durable_handoff durable;
    uint32_t sd_lease,low_terminal,low_heap_hook;
    struct kui_vmu_native_exit exits;
    uint32_t diagnostics;
#endif
};
#if KUI_VMU_NATIVE_PERSISTENT_PROBE && UINTPTR_MAX==UINT32_MAX
_Static_assert(sizeof(struct kui_vmu_native_passive_config)==2304u,"Persistent worker config ABI v4");
#endif
/* Frame order is shared with the SH gateway. R0 is dead at submission. */
struct kui_vmu_native_passive_frame {
    uint32_t gbr,macl,mach,pr,r14,r13,r12,r11,r10,r9,r8,r7,r6,r5,r4,r3,r2,sr,r1;
};
_Static_assert(sizeof(struct kui_vmu_native_passive_frame)==76u,"Passive frame ABI");
int kui_vmu_native_passive_arm(const struct kui_vmu_native_passive_config *,void *unused);
/* Persistent NORMAL/PROFILE polling may retain the original SDK DMA for a
 * bounded main-unit batch: A must be controller GETCOND, other distinct ports
 * may also use zero-payload INFO/ALLINFO. Empty ports and newly connected
 * devices remain physical; exact typed query responses with nonzero functions
 * and exact zero-word FC/FD/FE query errors pass through unchanged. After
 * complete source/response/context validation,
 * only A's virtual-subunit presence bit is added. These batches cannot issue
 * virtual replies or access storage. Subunits, VMU operations and other shapes
 * retain the complete filtered-overlay/storage proof path. */
void kui_vmu_native_passive_submit(const struct kui_vmu_native_passive_frame *);
void kui_vmu_native_passive_complete(const struct kui_vmu_native_passive_frame *);
void kui_vmu_native_passive_terminal(void) __attribute__((noreturn));
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
/* Failure-only gateway: masks IRQs and switches to the private worker stack
 * before reporting. It never resumes the owner or performs storage I/O. */
void kui_vmu_native_runtime_fault_hook(uint32_t,uint32_t,uint32_t,uint32_t) __attribute__((noreturn));
void kui_vmu_native_allocation_failure_hook(void) __attribute__((noreturn));
void kui_vmu_native_runtime_fault(uint32_t,uint32_t,uint32_t,uint32_t) __attribute__((noreturn));
#endif
#endif
