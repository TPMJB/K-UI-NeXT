/* SPDX-License-Identifier: GPL-3.0-only */
/* Compile with the native foreground reader ABI, never KUI_RETAIL_CE. The
 * accepted helper is not relinked with this independent terminal predicate. */
#include "kui/smash_ce_return_view.h"
#include "kui/armada_ce_vmu_write.h"
#include "kui/armada_ce_vmu_sd.h"
#include "kui/vc2_ce_vmu_handoff.h"
#include "retail_async.h"

#if !KUI_ARMADA_CE_VMU_WRITE_RUNTIME || !KUI_VC2_VMU_PERSIST || defined(KUI_RETAIL_CE)
#error "CE return view requires the persistent helper's native ABI"
#endif
KUI_VC2_VMU_NATIVE_LAYOUT_ASSERTS();

/* Exact anonymous objects in armada_ce_vmu_runtime.c and vc2_ce_vmu_loan.c.
 * The generator binds their complete sizes as well as their addresses. */
struct smash_ce_runtime_view {
    struct kui_armada_ce_vmu_write endpoint;
    struct kui_vmu_deferred_io queue;
    struct kui_armada_ce_vmu_sd sd;
    uint32_t started,ready,owner,handling,worker,faulted,busy_rounds;
    uint8_t read_cache[8][512];
    uint16_t read_tag[8];
};
struct smash_ce_loan_view {
    volatile kui_retail_async_region_type *region;
    const struct kui_retail_manifest *manifest;
    volatile uint32_t *active,*fault;
    uintptr_t begin,end;
    uint32_t magic,started,state,claims,releases,busy,first_fault,last_result;
};
struct smash_ce_fence_view {
    uint32_t pending,active,extension,source,saved_enable,trigger,previous_mode;
};
#ifdef __sh__
_Static_assert(sizeof(struct smash_ce_runtime_view)==0x2240u,"Frozen CE runtime view size");
_Static_assert(sizeof(struct smash_ce_loan_view)==56u,"Frozen CE loan view size");
_Static_assert(sizeof(struct smash_ce_fence_view)==28u,"Frozen CE fence view size");
_Static_assert(sizeof(struct kui_retail_manifest)==828u,"Frozen CE reader map size");
#endif

static int view_span(uint32_t p,size_t n,uint32_t begin,uint32_t end) {
    return !(p&3u) && p>=begin && p<end && n<=(size_t)(end-p);
}
static int view_overlap(uint32_t a,size_t an,uint32_t b,size_t bn) {
    return a<=b?(size_t)(b-a)<an:(size_t)(a-b)<bn;
}
static int view_zero(const volatile void *p,size_t bytes) {
    const volatile uint8_t *b=p;
    for(size_t i=0u;i<bytes;i++)if(b[i])return 0;
    return 1;
}
int kui_smash_ce_return_idle(const struct kui_smash_ce_return_view *input) {
    if(!input || ((uintptr_t)input&3u))return -1;
    const struct kui_smash_ce_return_view v=*input;
    const uint32_t at[5]={v.runtime,v.loan,v.continuation,v.fence,v.saved_region};
    const size_t size[5]={sizeof(struct smash_ce_runtime_view),sizeof(struct smash_ce_loan_view),
        4u,sizeof(struct smash_ce_fence_view),4u};
    for(unsigned i=0u;i<5u;i++) {
        if(!view_span(at[i],size[i],UINT32_C(0xacfc0000),UINT32_C(0xacfc4000)))return -1;
        for(unsigned j=0u;j<i;j++)if(view_overlap(at[i],size[i],at[j],size[j]))return -1;
    }
    if((v.runtime&31u) || v.freeze!=UINT32_C(0xacfcfe78) || (v.region&31u) ||
       !view_span(v.region,sizeof(kui_retail_async_region_type),UINT32_C(0xac004000),UINT32_C(0xac007ea0)) ||
       !view_span(v.manifest,sizeof(struct kui_retail_manifest),UINT32_C(0xac004000),UINT32_C(0xac007ea0)) ||
       view_overlap(v.region,sizeof(kui_retail_async_region_type),v.manifest,sizeof(struct kui_retail_manifest)))return -1;
    const volatile struct smash_ce_runtime_view *r=(const void *)(uintptr_t)v.runtime;
    const volatile struct smash_ce_loan_view *l=(const void *)(uintptr_t)v.loan;
    const volatile struct smash_ce_fence_view *f=(const void *)(uintptr_t)v.fence;
    const volatile kui_retail_async_region_type *low=(const void *)(uintptr_t)v.region;
    const volatile struct kui_retail_manifest *m=(const void *)(uintptr_t)v.manifest;
    const volatile struct kui_retail_gd *gd=&low->shared.service;
    uint32_t map_p1=v.manifest&~UINT32_C(0x20000000);
    if(*(volatile const uint32_t *)(uintptr_t)v.freeze ||
       *(volatile const uint32_t *)(uintptr_t)v.saved_region!=v.region ||
       l->magic!=UINT32_C(0x314c4356) || l->started!=1u || l->first_fault ||
       (uintptr_t)l->region!=v.region || (uintptr_t)l->manifest!=v.manifest ||
       (uintptr_t)l->active!=v.manifest+244u || (uintptr_t)l->fault!=v.manifest+240u ||
       l->begin!=UINT32_C(0x8c004000) || l->end!=UINT32_C(0x8c007ea0) ||
       l->state>3u || !l->state || l->state==3u ||
       (uintptr_t)low->engine.manifest!=map_p1 ||
       (uintptr_t)gd->tracks!=map_p1+offsetof(struct kui_retail_manifest,slots) ||
       (uintptr_t)gd->ops.context!=1u || !gd->initialized ||
       low->shared.port.fault || low->engine.failed ||
       low->shared.card.transport!=KUI_STORAGE_SCI || m->storage_transport!=KUI_STORAGE_SCI ||
       !low->shared.card.device.sd.ready || low->shared.card.device.sd.slow ||
       !low->shared.card.device.sd.blocks || low->shared.card.device.sd.blocks>UINT64_C(0x100000000) ||
       low->shared.card.device.sd.blocks!=m->card_sectors ||
       *(volatile const uint32_t *)(uintptr_t)(v.manifest+240u))return -1;
    if(l->state==2u || gd->pending || gd->executing || gd->callback_due ||
       low->engine.active || low->engine.in_irq || low->engine.opened || low->engine.hooked ||
       low->engine.release.armed || low->engine.release.depth || low->shared.port.acquired ||
       low->shared.stream.state || low->shared.card.stream.active ||
       *(volatile const uint32_t *)(uintptr_t)(v.manifest+244u))return 0;
    if(l->claims!=l->releases)return -1;
    uint32_t pending=*(volatile const uint32_t *)(uintptr_t)v.continuation;
    if(pending>1u || f->pending>1u || f->active>1u)return -1;
    if(pending || f->pending || f->active)return 0;
    if(!r->started)return view_zero(r,sizeof(*r))?1:-1;
    if(r->started!=1u || r->ready!=1u || !r->owner || (r->owner&3u) ||
       r->owner>=UINT32_C(0x80000000) || r->faulted || r->sd.faulted || r->sd.pio.fault ||
       r->endpoint.self!=&r->endpoint || r->endpoint.magic!=UINT32_C(0x31574341) ||
       r->endpoint.started!=1u || !r->endpoint.protocol.initialized || r->endpoint.protocol.killed ||
       r->queue.self!=&r->queue || r->queue.magic!=UINT32_C(0x314a4d56) ||
       r->queue.state>=KUI_VMU_DEFERRED_FAULT || r->queue.failures ||
       r->sd.self!=&r->sd || r->sd.magic!=UINT32_C(0x31534441) || r->sd.started!=1u ||
       r->sd.pio.self!=(uintptr_t)&r->sd.pio || r->sd.pio.magic!=UINT32_C(0x4b565031) ||
       r->sd.pio.captured>1u)return -1;
    if(r->handling || r->worker || r->endpoint.handling || r->endpoint.protocol.active ||
       r->endpoint.protocol.commit_attempted || r->queue.state!=KUI_VMU_DEFERRED_IDLE ||
       r->sd.busy || r->sd.store.io.context || r->sd.pio.acquired || r->sd.pio.selected)return 0;
    return 1;
}

/* Target-compiler constants for fixture construction and generator review.
 * This symbol and its storage do not exist in the production translation. */
#ifdef KUI_SMASH_CE_RETURN_VIEW_LAYOUT_PROBE
#define RT(member) offsetof(struct smash_ce_runtime_view,member)
#define LO(member) offsetof(struct smash_ce_loan_view,member)
#define LOW(member) offsetof(kui_retail_async_region_type,member)
#define MAP(member) offsetof(struct kui_retail_manifest,member)
const uint32_t kui_smash_ce_return_view_layout[]={
    sizeof(struct smash_ce_runtime_view),sizeof(struct smash_ce_loan_view),
    sizeof(kui_retail_async_region_type),sizeof(struct kui_retail_manifest),
    RT(started),RT(ready),RT(owner),RT(handling),RT(worker),RT(faulted),RT(busy_rounds),
    RT(endpoint.self),RT(endpoint.magic),RT(endpoint.started),RT(endpoint.handling),
    RT(endpoint.protocol.initialized),RT(endpoint.protocol.killed),RT(endpoint.protocol.active),
    RT(endpoint.protocol.commit_attempted),RT(endpoint.protocol.phases),
    RT(queue.self),RT(queue.magic),RT(queue.state),RT(queue.failures),
    RT(sd.self),RT(sd.magic),RT(sd.started),RT(sd.busy),RT(sd.faulted),RT(sd.bound),
    RT(sd.store.io.context),RT(sd.pio.self),RT(sd.pio.magic),RT(sd.pio.fault),
    RT(sd.pio.acquired),RT(sd.pio.captured),RT(sd.pio.selected),
    RT(sd.pio.mode),RT(sd.pio.data),RT(sd.pio.scr),RT(sd.pio.smr),RT(sd.pio.brr),
    RT(sd.pio.ptr),RT(sd.pio.standby),RT(sd.pio.stage),
    LO(region),LO(manifest),LO(active),LO(fault),LO(begin),LO(end),LO(magic),LO(started),
    LO(state),LO(claims),LO(releases),LO(first_fault),
    LOW(shared.service.initialized),LOW(shared.service.ops.context),LOW(shared.service.tracks),
    LOW(shared.service.pending),LOW(shared.service.executing),LOW(shared.service.callback_due),
    LOW(shared.service.error),LOW(engine.manifest),LOW(engine.active),LOW(engine.failed),
    LOW(engine.in_irq),LOW(engine.opened),LOW(engine.hooked),LOW(engine.release.armed),
    LOW(engine.release.depth),LOW(shared.port.acquired),LOW(shared.port.fault),
    LOW(shared.stream.state),LOW(shared.card.stream.active),LOW(shared.card.transport),
    LOW(shared.card.device.sd.ready),LOW(shared.card.device.sd.slow),LOW(shared.card.device.sd.blocks),
    MAP(storage_transport),MAP(card_sectors),offsetof(struct kui_retail_manifest,slots),240u,244u
};
#undef RT
#undef LO
#undef LOW
#undef MAP
#endif
