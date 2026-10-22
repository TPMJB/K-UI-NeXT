/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_DISCOVERY_H
#define KUI_VMU_NATIVE_DISCOVERY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_NATIVE_DISCOVERY_VERSION 1u
#define KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 1u
/* Discovery is never authority to patch or reserve game RAM. These three
 * capabilities must be supplied independently on every actual launch. */
#define KUI_VMU_NATIVE_NEEDS_MEMORY 1u
#define KUI_VMU_NATIVE_NEEDS_LIVE_STATE 2u
#define KUI_VMU_NATIVE_NEEDS_LIFETIME 4u
#define KUI_VMU_NATIVE_REQUIREMENTS 7u
enum kui_vmu_native_discovery_result {
    KUI_VMU_NATIVE_NOT_FOUND=0, KUI_VMU_NATIVE_RESOLVED=1,
    KUI_VMU_NATIVE_INVALID=-1, KUI_VMU_NATIVE_AMBIGUOUS=-2,
    KUI_VMU_NATIVE_INCOMPLETE=-3
};
/* Native fixed-width cache payload. An outer file codec must encode its
 * fields explicitly; do not serialize compiler-dependent struct layouts.
 * Preserve original U0/P1/P2 pointer values; code locations use load alias. */
struct kui_vmu_native_discovery {
    uint32_t version,bytes,family,image_bytes,load_address,requirements;
    uint32_t submit,resume,initializer,manager_cell,manager_initial,lock_cell;
    uint32_t completion_slot,completion_call,consumer,gate,ready,gate_register;
    uint8_t image_sha256[32];
};
/* Immutable loaded executable only; no MMIO access, game calls or writes.
 * Bounded to12MiB. Failure clears out except invalid overlapping buffers,
 * which are untouched. Exactly one complete candidate required. */
enum kui_vmu_native_discovery_result kui_vmu_native_discover(
    const void *image,size_t bytes,uint32_t load,struct kui_vmu_native_discovery *out);
/* Bind an untrusted cached resolution to the exact executable and schema,
 * replaying all witnesses and uniqueness. This reference path does not skip
 * the scan for speed and still supplies no runtime capability. */
bool kui_vmu_native_discovery_cache_valid(const struct kui_vmu_native_discovery *,
    const void *image,size_t bytes,uint32_t load);
#endif
