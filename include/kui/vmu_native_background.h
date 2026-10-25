/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_BACKGROUND_H
#define KUI_VMU_NATIVE_BACKGROUND_H
#include <stdbool.h>
#include <stdint.h>
#include "kui/native_readahead_layout.h"
#include "kui/native_dual_reader.h"

/* The ordinary native allocation owns 192 KiB. Its relocated worker and
 * stack occupy the first 128 KiB; the immutable 64-slot map and native
 * vector/receive/state region use the beginning of the remaining 64 KiB.
 * The isolated read-ahead profile owns 384 KiB with the same map/worker
 * geometry. Auxiliary code, stack and cache have disjoint owned spans.
 * Keep the region cache-line aligned. */
#define KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES UINT32_C(0x20000)
#if KUI_NATIVE_READAHEAD || KUI_NATIVE_DUAL_READERS
#define KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES KUI_NATIVE_RA_LEASE_BYTES
#else
#define KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES UINT32_C(0x30000)
#endif
#define KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES UINT32_C(1020)
#define KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET UINT32_C(0x20400)
#define KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES UINT32_C(0x760)
#if KUI_NATIVE_DUAL_READERS
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==KUI_NATIVE_DUAL_LEASE_BYTES,
    "Both dual low engines own the advertised complete lease");
#endif
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES+KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES<=
    KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET,"Background map excludes its vector region");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET+KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES<=
    KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES,"Background state fits the existing allocation tail");
#if KUI_NATIVE_READAHEAD
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET+KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES<=
    KUI_NATIVE_RA_AUX_OFFSET,"Read-ahead auxiliary excludes the retained reader region");
_Static_assert(KUI_NATIVE_RA_AUX_OFFSET+KUI_NATIVE_RA_AUX_SPAN==KUI_NATIVE_RA_STACK_END &&
    KUI_NATIVE_RA_STACK_OFFSET+UINT32_C(0x1000)==KUI_NATIVE_RA_STACK_END,
    "Read-ahead auxiliary owns its complete private stack");
_Static_assert(KUI_NATIVE_RA_STACK_END<=KUI_NATIVE_RA_DATA_OFFSET &&
    KUI_NATIVE_RA_DATA_OFFSET+KUI_NATIVE_RA_DATA_BYTES==KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES &&
    KUI_NATIVE_RA_DATA_BYTES==KUI_NATIVE_RA_BLOCKS*512u,
    "Read-ahead data arena is disjoint and ends at the selected lease boundary");
#endif

/* The installer retains its verified map until the first allocator hook.
 * This is a temporary P1 pointer, never permission to retain stage memory
 * once the worker has been installed. */
static inline bool kui_vmu_native_background_stage_map_address(uint32_t address) {
    return !(address&3u) && address>=UINT32_C(0x8ce00000) &&
        address<=UINT32_C(0x8cfe0000)-KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES;
}
static inline bool kui_vmu_native_background_stage_region_address(uint32_t address) {
    return !(address&31u) && address>=UINT32_C(0x8ce00000) &&
        address<=UINT32_C(0x8cfe0000)-KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES;
}
/* Allocation/lifetime validation remains the installer's responsibility.
 * This pure check admits only the exact P1 map tail of a RAM lease whose
 * complete selected payload is in main RAM. Accept the allocator's U0/P1/P2
 * aliases for the lease, but never a different alias for the retained map. */
static inline bool kui_vmu_native_background_map_address(uint32_t lease,uint32_t address) {
    uint32_t area=lease&UINT32_C(0xe0000000),physical=lease&UINT32_C(0x1fffffff);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        !(lease&31u) && physical>=UINT32_C(0x0c010020) &&
        physical<=UINT32_C(0x0d000000)-KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES &&
        address==(physical|UINT32_C(0x80000000))+KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES;
}
static inline bool kui_vmu_native_background_region_address(uint32_t lease,uint32_t address) {
    return kui_vmu_native_background_map_address(lease,
        address-(KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET-KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES));
}

struct kui_retail_manifest;
/* IMASK15 on the installer's stack, after its verified copy into the lease
 * tail. This retires both stage workspaces and rebases their internal pointers;
 * tokens, completed output, idle CMD18 state and bus ownership are preserved. */
int kui_vmu_native_background_manifest_bind(const struct kui_retail_manifest *temporary,
                                          const struct kui_retail_manifest *owned);
#endif
