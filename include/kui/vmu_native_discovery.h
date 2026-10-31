/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_DISCOVERY_H
#define KUI_VMU_NATIVE_DISCOVERY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_VMU_NATIVE_DISCOVERY_VERSION 1u
#define KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 1u
/* pd 1.46 (Shinobi 1.41-1.43): the same submit, consumer, gate and ready code.
 * Its Maple server does not mask interrupts: it takes the driver lock with
 * TAS.B and sets a busy word, and the API wrappers may run it at their own
 * interrupt level. The lock pointer stays the image's cached one, and the
 * server entry is completion_call-0x3a instead of -0x4a. Discovery selects it only from
 * the exact server entry and lock helper; every other image is UNIFORM_V1. */
#define KUI_VMU_NATIVE_FAMILY_EARLY_V1 2u
static inline bool kui_vmu_native_family_valid(uint32_t family) {
    return family==KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 || family==KUI_VMU_NATIVE_FAMILY_EARLY_V1;
}
/* Bytes from the Maple server entry to its completion call. */
static inline uint32_t kui_vmu_native_server_span(uint32_t family) {
    return family==KUI_VMU_NATIVE_FAMILY_EARLY_V1?0x3au:0x4au;
}
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
/* The lock pointer the initializer leaves in the lock cell: pd 1.50 stores
 * the P2 alias of the byte after the cell; pd 1.46 keeps the image's own
 * cached pointer, lock_cell+4 in the literal's U0 or P1 area. */
static inline uint32_t kui_vmu_native_lock_pointer(const struct kui_vmu_native_discovery *d) {
    return d->family==KUI_VMU_NATIVE_FAMILY_EARLY_V1?d->lock_cell+4u:
        ((d->lock_cell&UINT32_C(0x1fffffff))+4u)|UINT32_C(0xa0000000);
}
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
