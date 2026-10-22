/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_LIFETIME_H
#define KUI_VMU_NATIVE_LIFETIME_H
#include "kui/vmu_native_heap.h"

/* Necessary admission for the discovered tail32 allocator's original release
 * entry. The caller holds IMASK15 across this check and the original release,
 * supplies coherent owner reads, and intercepts every heap reinitializer before
 * it can reclaim the worker. No code or memory is modified here.
 *
 * A null release is allowed only while the complete worker lease is valid.
 * Nonnull payloads must retain the arena's actual virtual alias: this allocator
 * orders its free list using raw pointers. The candidate header/size must fit
 * the managed arena, exclude the protected lease/header, and not overlap any
 * current free block. Lease ownership and candidate exclusion share one live
 * bounded free-list walk. These checks do not establish that an arbitrary aligned
 * pointer was returned by allocate; the SDK's ordinary valid-free contract
 * remains necessary. A false result requires stopping before original code. */
bool kui_vmu_native_lifetime_release_safe(
    const struct kui_vmu_native_heap_lease *lease,
    kui_vmu_native_heap_read read,void *context,
    uint32_t initializations,uint32_t payload);
struct kui_vmu_native_lifetime_range {uint32_t begin,bytes;};
#define KUI_VMU_NATIVE_LIFETIME_RANGES 64u
/* Same admission plus exclusion of SDK-owned RX/TX/control spans. Ranges must
 * describe valid nonempty direct RAM spans; aliases are compared physically.
 * Input ranges are caller-owned and remain immutable throughout the check.
 * NULL with count0 preserves the original API. NULL payload still validates
 * every range and the complete lease before succeeding. */
bool kui_vmu_native_lifetime_release_safe_ranges(
    const struct kui_vmu_native_heap_lease *lease,
    kui_vmu_native_heap_read read,void *context,
    uint32_t initializations,uint32_t payload,
    const struct kui_vmu_native_lifetime_range *ranges,size_t count);
#endif
