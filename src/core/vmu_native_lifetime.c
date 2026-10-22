/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_lifetime.h"

static uint32_t physical(uint32_t address) {return address&UINT32_C(0x1fffffff);}
static bool overlap(uint32_t first,uint32_t end,uint32_t other,uint32_t other_end) {
    return first<other_end && other<end;
}
bool kui_vmu_native_lifetime_release_safe(
    const struct kui_vmu_native_heap_lease *lease,
    kui_vmu_native_heap_read read,void *context,
    uint32_t initializations,uint32_t payload) {
    if(!lease || !read || !kui_vmu_native_heap_lease_valid(lease,read,context,initializations))
        return false;
    if(!payload) return true;
    uint32_t alias=lease->begin&UINT32_C(0xe0000000);
    uint32_t first=physical(lease->begin),end=first+lease->heap_bytes;
    uint32_t actual=physical(payload),units;
    if((payload&UINT32_C(0xe0000000))!=alias || (actual&31u) ||
       actual<first+32u || actual>end) return false;
    uint32_t header=actual-32u;
    uint32_t guard=physical(lease->header),guard_end=physical(lease->payload)+lease->lease_bytes;
    if((actual>=guard && actual<guard_end) ||
       !read(context,(header|alias)+4u,&units) || !units || units>(end-header)/32u)
        return false;
    uint32_t candidate_end=header+units*32u;
    if(overlap(header,candidate_end,guard,guard_end)) return false;

    /* A second bounded walk excludes ordinary double-free/coalesced-free
     * inputs. The first walk above proves the worker still owns its lease;
     * neither check substitutes for the caller's uninterrupted ownership. */
    uint32_t node,previous_end=0u;
    if(!read(context,lease->owner.sentinel,&node)) return false;
    for(unsigned i=0;i<KUI_VMU_NATIVE_HEAP_FREE_LIMIT;i++) {
        if(node==lease->owner.sentinel) return true;
        uint32_t at=physical(node),next;
        if((node&UINT32_C(0xe0000000))!=alias || (at&31u) || at<first || at>=end ||
           (previous_end && at<previous_end) || !read(context,node+4u,&units) ||
           !units || units>(end-at)/32u || !read(context,node,&next)) return false;
        uint32_t free_end=at+units*32u;
        if(overlap(header,candidate_end,at,free_end)) return false;
        previous_end=free_end;node=next;
    }
    return false;
}
