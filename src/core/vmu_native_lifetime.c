/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_lifetime.h"

static uint32_t physical(uint32_t address) {return address&UINT32_C(0x1fffffff);}
static bool overlap(uint32_t first,uint32_t end,uint32_t other,uint32_t other_end) {
    return first<other_end && other<end;
}
bool kui_vmu_native_lifetime_release_safe_ranges(
    const struct kui_vmu_native_heap_lease *lease,
    kui_vmu_native_heap_read read,void *context,
    uint32_t initializations,uint32_t payload,
    const struct kui_vmu_native_lifetime_range *ranges,size_t count) {
    if(count>KUI_VMU_NATIVE_LIFETIME_RANGES || (count && !ranges) ||
       (count && (uintptr_t)ranges>UINTPTR_MAX-count*sizeof(*ranges))) return false;
    for(size_t i=0;i<count;i++) {
        uint32_t area=ranges[i].begin&UINT32_C(0xe0000000),at=physical(ranges[i].begin);
        if((area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
           !ranges[i].bytes || at<UINT32_C(0x0c000000) || at>=UINT32_C(0x0d000000) ||
           ranges[i].bytes>UINT32_C(0x0d000000)-at) return false;
    }
    uint32_t bytes;
    if(!kui_vmu_native_heap_lease_valid_with_release(lease,read,context,
        initializations,payload,&bytes)) return false;
    if(!payload) return true;
    uint32_t header=physical(payload)-32u,candidate_end=header+bytes;
    for(size_t i=0;i<count;i++) {
        uint32_t at=physical(ranges[i].begin);
        if(overlap(header,candidate_end,at,at+ranges[i].bytes)) return false;
    }

    return true;
}
bool kui_vmu_native_lifetime_release_safe(
    const struct kui_vmu_native_heap_lease *lease,
    kui_vmu_native_heap_read read,void *context,
    uint32_t initializations,uint32_t payload) {
    return kui_vmu_native_lifetime_release_safe_ranges(lease,read,context,
        initializations,payload,NULL,0u);
}
