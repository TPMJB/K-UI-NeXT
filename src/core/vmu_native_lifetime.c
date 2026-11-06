/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_lifetime.h"
#include <string.h>

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

static bool objects_separate(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t ap=(uintptr_t)a,bp=(uintptr_t)b;
    if(an>UINTPTR_MAX-ap || bn>UINTPTR_MAX-bp ||
       (ap<bp+bn && bp<ap+an)) return false;
#if UINTPTR_MAX > UINT32_MAX
    if(ap>UINT32_MAX || bp>UINT32_MAX) return true;
#endif
    uint32_t aa=(uint32_t)ap&UINT32_C(0xe0000000),ba=(uint32_t)bp&UINT32_C(0xe0000000);
    uint32_t pa=physical((uint32_t)ap),pb=physical((uint32_t)bp);
    bool ar=(aa==0u || aa==UINT32_C(0x80000000) || aa==UINT32_C(0xa0000000)) &&
        pa>=UINT32_C(0x0c000000) && pa<UINT32_C(0x0d000000) && an<=UINT32_C(0x0d000000)-pa;
    bool br=(ba==0u || ba==UINT32_C(0x80000000) || ba==UINT32_C(0xa0000000)) &&
        pb>=UINT32_C(0x0c000000) && pb<UINT32_C(0x0d000000) && bn<=UINT32_C(0x0d000000)-pb;
    return !ar || !br || (pa+an<=pb || pb+bn<=pa);
}
bool kui_vmu_native_lifetime_prepare(struct kui_vmu_native_lifetime_plan *out,
    const struct kui_vmu_native_heap_lease *lease,kui_vmu_native_heap_read read,
    void *context,uint32_t initializations,
    const struct kui_vmu_native_lifetime_range *ranges,size_t count) {
    if(!out || !lease || count>KUI_VMU_NATIVE_LIFETIME_RANGES || (count && !ranges) ||
       !objects_separate(out,sizeof(*out),lease,sizeof(*lease)) ||
       (count && !objects_separate(out,sizeof(*out),ranges,count*sizeof(*ranges))) ||
       !kui_vmu_native_heap_lease_valid(lease,read,context,initializations)) return false;
    struct kui_vmu_native_lifetime_plan p={0};
    p.self=(uintptr_t)out;p.alias=lease->begin&UINT32_C(0xe0000000);
    bool arena=lease->owner.family==KUI_VMU_NATIVE_HEAP_ARENA;
    if(arena) {
        /* No allocator: no release is ever admitted, so sentinel and cursor
         * stay zero. Identity is the worker's own first eight header words at
         * the arena's start, which nothing else writes; no protected SDK span
         * may lie inside the arena. */
        p.begin=p.lease_begin=physical(lease->payload);p.end=p.lease_end=p.begin+lease->lease_bytes;
        for(unsigned i=0;i<8u;i++) {
            uint32_t address=lease->payload+4u*i,value;
            if(!read || !read(context,address,&value)) return false;
            p.identity[i]=(struct kui_vmu_native_lifetime_witness){address,value};
        }
    } else {
        p.begin=physical(lease->begin);p.end=p.begin+lease->heap_bytes;
        p.lease_begin=physical(lease->header);p.lease_end=physical(lease->payload)+lease->lease_bytes;
        p.sentinel=lease->owner.sentinel;p.cursor=lease->owner.cursor_cell;
        const struct kui_vmu_native_heap *h=&lease->owner;
        /* Direct 1.35 has no dispatch roles. Its three original allocator
         * literal-pool words bind the same control cells and remain outside
         * both entry hooks. Fill the existing plan directly: two temporary
         * arrays would duplicate these eight witnesses on the cold stack. */
        p.identity[0]=(struct kui_vmu_native_lifetime_witness){h->begin_cell,lease->begin};
        p.identity[1]=(struct kui_vmu_native_lifetime_witness){h->begin_copy_cell,lease->begin};
        p.identity[2]=(struct kui_vmu_native_lifetime_witness){h->bytes_cell,lease->heap_bytes};
        if(h->family==KUI_VMU_NATIVE_HEAP_DIRECT32) {
            p.identity[3]=(struct kui_vmu_native_lifetime_witness){h->initializer+0x14cu,h->begin_cell};
            p.identity[4]=(struct kui_vmu_native_lifetime_witness){h->initializer+0x150u,h->begin_copy_cell};
            p.identity[5]=(struct kui_vmu_native_lifetime_witness){h->initializer+0x154u,h->bytes_cell};
        } else {
            p.identity[3]=(struct kui_vmu_native_lifetime_witness){h->dispatch_cell,h->dispatch_table};
            p.identity[4]=(struct kui_vmu_native_lifetime_witness){h->dispatch_table,h->allocate};
            p.identity[5]=(struct kui_vmu_native_lifetime_witness){h->dispatch_table+4u,h->release};
        }
        p.identity[6]=(struct kui_vmu_native_lifetime_witness){h->sentinel+4u,0u};
        p.identity[7]=(struct kui_vmu_native_lifetime_witness){lease->header+4u,lease->units};
    }
    for(size_t i=0;i<count;i++) {
        uint32_t area=ranges[i].begin&UINT32_C(0xe0000000),at=physical(ranges[i].begin);
        if((area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
           !ranges[i].bytes || at<UINT32_C(0x0c000000) || at>=UINT32_C(0x0d000000) ||
           ranges[i].bytes>UINT32_C(0x0d000000)-at) return false;
        uint32_t end=at+ranges[i].bytes;
        if(at<p.begin) at=p.begin;
        if(end>p.end) end=p.end;
        if(at>=end) continue;
        if(arena) return false;
        unsigned j=p.count++;
        while(j && p.ranges[j-1u].begin>at) {p.ranges[j]=p.ranges[j-1u];--j;}
        p.ranges[j]=(struct kui_vmu_native_lifetime_interval){at,end};
    }
    unsigned merged=0u;
    for(unsigned i=0;i<p.count;i++) {
        if(merged && p.ranges[i].begin<=p.ranges[merged-1u].end) {
            if(p.ranges[i].end>p.ranges[merged-1u].end) p.ranges[merged-1u].end=p.ranges[i].end;
        } else p.ranges[merged++]=p.ranges[i];
    }
    p.count=merged;p.ready=KUI_VMU_NATIVE_LIFETIME_READY;*out=p;return true;
}
bool kui_vmu_native_lifetime_release_prepared(
    const struct kui_vmu_native_lifetime_plan *plan,kui_vmu_native_heap_read read,
    void *context,uint32_t initializations,uint32_t payload) {
    return kui_vmu_native_lifetime_release_prepared_inline(plan,read,context,initializations,payload);
}
