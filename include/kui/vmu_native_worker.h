/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_WORKER_H
#define KUI_VMU_NATIVE_WORKER_H
#include "kui/vmu_native_heap.h"
#define KUI_VMU_NATIVE_WORKER_MAGIC UINT32_C(0x4b4e5731)
#define KUI_VMU_NATIVE_WORKER_VERSION 1u
#define KUI_VMU_NATIVE_WORKER_BYTES 0x10000u
#define KUI_VMU_NATIVE_WORKER_STACK_BYTES 4096u
#define KUI_VMU_NATIVE_WORKER_BLANK_CRC UINT32_C(0x3d826179)
struct kui_vmu_native_worker_exports {
    uint32_t magic,version,bytes,entry,bss_begin,bss_end,stack_bottom,stack_top,memory_end,reserved[7];
};
_Static_assert(sizeof(struct kui_vmu_native_worker_exports)==64u,"Native worker exports ABI");
struct kui_vmu_native_worker_config {
    uint32_t version,bytes,lease_address,lease_bytes,worker_span,initializations;
    const struct kui_vmu_native_heap_lease *lease;
};
struct kui_vmu_native_worker_result {
    uint32_t version,bytes,status,entry_sp,entry_sr,self_address,stack_low,stack_high;
    uint32_t requests,reads,refused,image_crc,stack_guard,lease_valid;
};
/* Isolated worker execution proof: no Maple interception or storage callbacks. */
int kui_vmu_native_worker_probe(const struct kui_vmu_native_worker_config *,struct kui_vmu_native_worker_result *);
/* CPU-only check, sharing the exact protocol exercised by the SH worker. */
int kui_vmu_native_worker_protocol_probe(struct kui_vmu_native_worker_result *);
int kui_vmu_native_worker_call(uint32_t entry_p2,uint32_t stack_top_p2,
    const struct kui_vmu_native_worker_config *,struct kui_vmu_native_worker_result *);
#endif
