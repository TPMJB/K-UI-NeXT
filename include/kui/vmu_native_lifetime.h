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

/* Launch-local immutable scalar preparation for the same complete admission.
 * Prepare only in the final private worker object, disjoint from its inputs.
 * Never serialize, relocate, fabricate or mutate a prepared object. All lease
 * metadata and input range geometry are proved once; live arena/dispatch/header
 * identity, the candidate, cursor and every current free block are still read
 * on every release. The clipped/coalesced intervals represent exactly the
 * original protected spans intersected with the candidate's managed arena.
 * Only the original reviewed valid-free/reinitializer contract is supported. */
struct kui_vmu_native_lifetime_witness {uint32_t address,value;};
struct kui_vmu_native_lifetime_interval {uint32_t begin,end;};
struct kui_vmu_native_lifetime_plan {
    uintptr_t self;
    uint32_t ready,count,alias,begin,end,lease_begin,lease_end,sentinel,cursor;
    struct kui_vmu_native_lifetime_witness identity[8];
    struct kui_vmu_native_lifetime_interval ranges[KUI_VMU_NATIVE_LIFETIME_RANGES];
};
#define KUI_VMU_NATIVE_LIFETIME_READY UINT32_C(0x4e4c4631)
bool kui_vmu_native_lifetime_prepare(struct kui_vmu_native_lifetime_plan *,
    const struct kui_vmu_native_heap_lease *,kui_vmu_native_heap_read,void *,
    uint32_t initializations,const struct kui_vmu_native_lifetime_range *,size_t count);

/* Every reader invocation below is an aligned, admitted direct main-RAM word.
 * A target reader may therefore omit general address-span checks. Static
 * witness/sentinel/cursor addresses were admitted at prepare; candidate/node
 * aliases, alignment and arena bounds are checked before following them.
 * Keep this one canonical body inline so a fixed private reader can resolve to
 * direct loads without a per-node callback trampoline. Generic callers may use
 * the external wrapper; both paths run this same complete dynamic proof. */
/* Identity alone is not free-list ownership admission. It is suitable only
 * between full proofs under the same continuously intercepted allocator
 * contract as kui_vmu_native_heap_lease_identity_valid. */
static inline __attribute__((always_inline)) bool
kui_vmu_native_lifetime_identity_prepared_inline(
    const struct kui_vmu_native_lifetime_plan *p,kui_vmu_native_heap_read read,
    void *context,uint32_t initializations) {
    if(!p || p->self!=(uintptr_t)p || p->ready!=KUI_VMU_NATIVE_LIFETIME_READY ||
       p->count>KUI_VMU_NATIVE_LIFETIME_RANGES || !read || initializations!=1u) return false;
    for(unsigned i=0;i<8u;i++) {
        uint32_t value;
        if(!read(context,p->identity[i].address,&value) || value!=p->identity[i].value) return false;
    }
    return true;
}
static inline __attribute__((always_inline)) bool
kui_vmu_native_lifetime_release_prepared_inline(
    const struct kui_vmu_native_lifetime_plan *p,kui_vmu_native_heap_read read,
    void *context,uint32_t initializations,uint32_t payload) {
    if(!kui_vmu_native_lifetime_identity_prepared_inline(p,read,context,initializations)) return false;
    /* These immutable scalars stay out of the dynamic node loop. */
    const uint32_t alias=p->alias,begin=p->begin,end=p->end;
    const uint32_t guard=p->lease_begin,guard_end=p->lease_end,sentinel=p->sentinel;
    uint32_t release_begin=0u,release_end=0u;
    if(payload) {
        uint32_t at=payload&UINT32_C(0x1fffffff),units;
        if((payload&UINT32_C(0xe0000000))!=alias || (at&31u) ||
           at<begin+32u || at>end) return false;
        release_begin=at-32u;
        if((at>=guard && at<guard_end) ||
           !read(context,(release_begin|alias)+4u,&units) || !units ||
           units>(end-release_begin)/32u) return false;
        release_end=release_begin+units*32u;
        if(release_begin<guard_end && guard<release_end) return false;
        for(unsigned i=0;i<p->count;i++)
            if(release_begin<p->ranges[i].end && p->ranges[i].begin<release_end) return false;
    }
    uint32_t node,cursor,previous_end=0u;
    if(!read(context,sentinel,&node) || !read(context,p->cursor,&cursor)) return false;
    bool cursor_seen=cursor==sentinel;
    for(unsigned i=0;i<KUI_VMU_NATIVE_HEAP_FREE_LIMIT;i++) {
        if(node==sentinel) return cursor_seen;
        uint32_t at=node&UINT32_C(0x1fffffff),area=node&UINT32_C(0xe0000000),units,next;
        /* Alignment plus at<end<=RAM_END proves both node words fit RAM. */
        if((payload?(area!=alias):(area!=0u && area!=UINT32_C(0x80000000) &&
           area!=UINT32_C(0xa0000000))) || (node&31u) || at<begin || at>=end ||
           (previous_end && at<previous_end) || !read(context,node+4u,&units) ||
           !units || units>(end-at)/32u) return false;
        uint32_t node_end=at+units*32u;
        if((at<guard_end && guard<node_end) ||
           (payload && at<release_end && release_begin<node_end) ||
           !read(context,node,&next)) return false;
        if(node==cursor) cursor_seen=true;
        previous_end=node_end;node=next;
    }
    return false;
}
bool kui_vmu_native_lifetime_release_prepared(
    const struct kui_vmu_native_lifetime_plan *,kui_vmu_native_heap_read,void *,
    uint32_t initializations,uint32_t payload);
#endif
