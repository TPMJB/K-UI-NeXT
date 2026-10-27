/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_OWNER_POINTER_H
#define KUI_ARMADA_CE_MAPLE_OWNER_POINTER_H
#include <stdbool.h>
#include <stdint.h>

/* Exact Armada MapPtrToProcess low-pointer rule, narrowed to aligned heap
 * words in one validated owner slot. This changes only address metadata:
 * no table/value read, native permission, allocation or object lease follows.
 * The caller separately proves the owner and slot0/owner table-root alias. */
static inline bool kui_armada_ce_maple_owner_pointer(
    uint32_t raw,uint32_t owner_slot,uint32_t bytes,uint32_t *out) {
    if(!out) return false;
    *out=0;
    if(owner_slot<UINT32_C(0x02000000) || owner_slot>UINT32_C(0x40000000) ||
       (owner_slot&UINT32_C(0x01ffffff)) || !bytes || (bytes&3u) || (raw&3u)) return false;
    uint32_t offset=raw&UINT32_C(0x01ffffff);
    if(offset<=UINT32_C(0x10000) ||
       (raw>=UINT32_C(0x02000000) && (raw&UINT32_C(0xfe000000))!=owner_slot) ||
       bytes>UINT32_C(0x02000000)-offset) return false;
    *out=owner_slot|offset;
    return true;
}
#endif
