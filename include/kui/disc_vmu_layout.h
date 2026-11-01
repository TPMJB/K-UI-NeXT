/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_VMU_LAYOUT_H
#define KUI_DISC_VMU_LAYOUT_H
#include <stdbool.h>
#include <stdint.h>

/* One original-game allocator lease. The existing compact worker retains its
 * complete 72 KiB image/state/private stack. The independent serial save owner
 * occupies a separate 8 KiB tail; neither uses the BIOS's low firmware RAM. */
#define KUI_DISC_VMU_WORKER_BYTES UINT32_C(0x12000)
#define KUI_DISC_VMU_SUPPORT_OFFSET KUI_DISC_VMU_WORKER_BYTES
#define KUI_DISC_VMU_SUPPORT_BYTES UINT32_C(0x2000)
#define KUI_DISC_VMU_LEASE_BYTES (KUI_DISC_VMU_WORKER_BYTES+KUI_DISC_VMU_SUPPORT_BYTES)

/* A shape predicate only: relocation, exact export pinning and live lease
 * ownership are independently proved by the installer and runtime worker. */
static inline bool kui_disc_vmu_support_entry_owned(uint32_t lease,uint32_t target) {
    uint32_t area=lease&UINT32_C(0xe0000000),p=lease&UINT32_C(0x1fffffff);
    if((area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       (lease&31u) || p<UINT32_C(0x0c010020) ||
       p>UINT32_C(0x0d000000)-KUI_DISC_VMU_LEASE_BYTES ||
       (target&1u) || (target&UINT32_C(0xe0000000))!=UINT32_C(0x80000000)) return false;
    uint32_t first=(p|UINT32_C(0x80000000))+KUI_DISC_VMU_SUPPORT_OFFSET;
    return target>=first+64u && target<first+KUI_DISC_VMU_SUPPORT_BYTES;
}
#endif
