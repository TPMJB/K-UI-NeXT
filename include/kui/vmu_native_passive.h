/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_PASSIVE_H
#define KUI_VMU_NATIVE_PASSIVE_H
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_runtime.h"
#include "kui/vmu_native_layout.h"
#define KUI_VMU_NATIVE_PASSIVE_MAGIC UINT32_C(0x4b4e5031)
#define KUI_VMU_NATIVE_PASSIVE_SPAN 0x10000u
struct kui_vmu_native_passive_exports {
    uint32_t magic,version,bytes,arm,submit_hook,complete_hook,terminal;
    uint32_t bss_begin,bss_end,stack_bottom,stack_top,memory_end;
    uint32_t resume_cell,consumer_cell,reserved[2];
};
_Static_assert(sizeof(struct kui_vmu_native_passive_exports)==64u,"Passive worker exports ABI");
struct kui_vmu_native_passive_config {
    uint32_t version,bytes,tx_base;
    struct kui_vmu_native_discovery driver;
    struct kui_vmu_native_heap_lease lease;
    struct kui_vmu_native_layout layout;
    struct kui_vmu_native_runtime_config runtime;
    uint32_t display[14];
};
/* Frame order is shared with the SH gateway. R0 is dead at submission. */
struct kui_vmu_native_passive_frame {
    uint32_t gbr,macl,mach,pr,r14,r13,r12,r11,r10,r9,r8,r7,r6,r5,r4,r3,r2,sr,r1;
};
_Static_assert(sizeof(struct kui_vmu_native_passive_frame)==76u,"Passive frame ABI");
int kui_vmu_native_passive_arm(const struct kui_vmu_native_passive_config *,void *unused);
void kui_vmu_native_passive_submit(const struct kui_vmu_native_passive_frame *);
void kui_vmu_native_passive_complete(const struct kui_vmu_native_passive_frame *);
void kui_vmu_native_passive_terminal(void) __attribute__((noreturn));
#endif
