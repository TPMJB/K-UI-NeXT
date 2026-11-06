/* SPDX-License-Identifier: GPL-3.0-only */
/* This translation unit is compiled with the *native foreground* structure
 * flags, independently from the CE Maple helper. A CE-flags cast of this
 * region would read the wrong service/stream fields. */
#include "retail_async.h"
#include "kui/vc2_ce_vmu_handoff.h"
#if KUI_VC2_VMU_PERSIST
#include "kui/vc2_ce_vmu_persist.h"
#include "kui/vc2_ce_vmu_late.h"
#include "kui/vc2_ce_vmu_loan.h"
#include "kui/armada_ce_vmu_runtime.h"
#include <string.h>
extern struct kui_vmu_durable_handoff kui_armada_ce_vmu_runtime_handoff;
extern struct kui_armada_ce_vmu_runtime_services kui_armada_ce_vmu_runtime_services;
#endif

KUI_VC2_VMU_NATIVE_LAYOUT_ASSERTS();
static const kui_retail_async_region_type *vc2_saved_region;

/* The low caller passes its linked trace symbol in R6. Admit that pointer
 * again against both independent native map references, not an arbitrary
 * offset into a supplied allocation. The low ELF aliases its trace exactly
 * to manifest.title; both native consumers assert that source ABI above. */
uint32_t kui_vc2_ce_vmu_service_refuse(void *region,volatile void *trace,uint32_t reason) {
    uintptr_t pointer=(uintptr_t)region,at=(uintptr_t)trace;
    if(!pointer || !at)return KUI_VC2_VMU_GATE_INITIALIZER;
#ifndef KUI_RETAIL_ASYNC_TEST
    if((pointer&UINT32_C(0xe000001f))!=UINT32_C(0x80000000) ||
       pointer<UINT32_C(0x8c004000) ||
       pointer>UINT32_C(0x8c007ea0)-sizeof(kui_retail_async_region_type) ||
       (at&UINT32_C(0xe0000003))!=UINT32_C(0x80000000) ||
       at<UINT32_C(0x8c004000) || at>UINT32_C(0x8c007ea0)-148u)
        return KUI_VC2_VMU_GATE_INITIALIZER;
    for(uintptr_t line=pointer;line<pointer+sizeof(kui_retail_async_region_type);line+=32u)
        __asm__ volatile("ocbp @%0"::"r"(line):"memory");
    pointer|=UINT32_C(0x20000000);
#endif
    const kui_retail_async_region_type *r=(const void *)pointer;
    const uintptr_t map=(uintptr_t)r->engine.manifest;
    if(!map || at!=map+offsetof(struct kui_retail_manifest,title) ||
       (uintptr_t)r->shared.service.tracks!=map+offsetof(struct kui_retail_manifest,slots))
        return KUI_VC2_VMU_GATE_INITIALIZER;
#ifndef KUI_RETAIL_ASYNC_TEST
    /* Current gateway capture may still be dirty through P1. Publish and
     * invalidate it before the P2 result, so neither the low renderer nor
     * the gateway's final writeback can resurrect its previous contents. */
    for(uintptr_t line=at&~(uintptr_t)31u;line<at+148u;line+=32u)
        __asm__ volatile("ocbp @%0"::"r"(line):"memory");
    at|=UINT32_C(0x20000000);
#endif
    volatile uint32_t *words=(volatile void *)at;
    words[3]=KUI_VC2_VMU_INIT_P2;
    words[14]=reason;
    words[15]=KUI_VC2_VMU_GATE_INITIALIZER;
    return KUI_VC2_VMU_GATE_INITIALIZER;
}

uint32_t kui_vc2_ce_vmu_service_validate(void *region,uint32_t token) {
    if(!region)return KUI_VC2_VMU_GATE_STREAM;
    uintptr_t pointer=(uintptr_t)region;
#ifndef KUI_RETAIL_ASYNC_TEST
    if((pointer&UINT32_C(0xe000001f))!=UINT32_C(0x80000000) ||
       pointer<UINT32_C(0x8c004000) ||
       pointer>UINT32_C(0x8c007ea0)-sizeof(kui_retail_async_region_type))
        return KUI_VC2_VMU_GATE_STREAM;
    /* The gateway publishes the region on return. This check runs inside
     * its current call, so explicitly publish before retaining its P2 view. */
    for(uintptr_t at=pointer;at<pointer+sizeof(kui_retail_async_region_type);at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    pointer|=UINT32_C(0x20000000);
#endif
    const kui_retail_async_region_type *r=(const void *)pointer;
    const struct kui_retail_gd *s=&r->shared.service;
    if(!s->initialized || s->ops.context || !token || token!=s->token ||
       s->command!=KUI_RETAIL_GD_DMAREAD_LEGACY_STREAM || s->error ||
       s->area!=KUI_VC2_VMU_KERNEL_FAD || s->sector_bytes!=2048u ||
       s->request_bytes!=KUI_VC2_VMU_KERNEL_BYTES ||
       s->completed_bytes!=KUI_VC2_VMU_BODY_BYTES || s->outputs[0]!=0x800u ||
       (s->destination&UINT32_C(0x1fffffff))!=UINT32_C(0x0c010000) ||
       s->count || s->pending || s->executing ||
       s->status!=KUI_RETAIL_GD_LEGACY_STREAMING)
        return KUI_VC2_VMU_GATE_STREAM;
    if(r->engine.active || r->engine.failed || r->engine.in_irq ||
       r->engine.hooked || r->engine.opened || r->engine.release.armed ||
       r->engine.release.depth || r->shared.port.acquired ||
       r->shared.port.fault || r->shared.stream.state || r->shared.card.stream.active)
        return KUI_VC2_VMU_GATE_SCI;
    vc2_saved_region=r;
    return 0u;
}

/* The installer calls this only after every source check and successful
 * hook publication. P2 matches the low region's publication contract; its
 * map_guest callback treats this marker as the retained high-gap owner. */
uint32_t kui_vc2_ce_vmu_service_own(void) {
    volatile kui_retail_async_region_type *r=(volatile void *)vc2_saved_region;
    if(!r)return KUI_VC2_VMU_GATE_OWNERSHIP;
    r->shared.service.ops.context=(void *)(uintptr_t)1u;
#if KUI_VC2_VMU_PERSIST
    const struct kui_retail_manifest *m=(const void *)
        ((uintptr_t)r->engine.manifest|UINT32_C(0x20000000));
    struct kui_vc2_ce_vmu_persist_config *c=(void *)(uintptr_t)KUI_ARMADA_CE_VMU_WORKER_STACK_BOTTOM;
    const kui_vc2_vmu_late_read_block read=(kui_vc2_vmu_late_read_block)(uintptr_t)KUI_VC2_LATE_RAW_P2;
    if(!m || !m->gdi_crc32 || m->gdi_crc32>KUI_VC2_PERSIST_CAPSULE_BLOCKS)
        return KUI_VC2_VMU_GATE_OWNERSHIP;
    for(uint32_t i=0u;i<KUI_VC2_PERSIST_CONFIG_BLOCKS;i++) {
        uint32_t block=KUI_VC2_PERSIST_HELPER_BLOCKS+i;
        uint32_t lba=block<m->gdi_crc32?m->boot_lba+block:m->boot_bytes+block-m->gdi_crc32;
        if(read(lba,(uint8_t *)c+512u*i))return KUI_VC2_VMU_GATE_LOAD;
    }
    if(kui_retail_crc32(0u,c,sizeof(*c))!=m->boot_crc32 ||
       !kui_vc2_ce_vmu_persist_config_valid(c))return KUI_VC2_VMU_GATE_OWNERSHIP;
    memcpy(&kui_armada_ce_vmu_runtime_handoff,&c->handoff,sizeof(c->handoff));
    struct kui_armada_ce_vmu_runtime_services *services=&kui_armada_ce_vmu_runtime_services;
    services->loan_begin=(uint32_t)(uintptr_t)kui_vc2_ce_vmu_loan_begin;
    services->loan_end=(uint32_t)(uintptr_t)kui_vc2_ce_vmu_loan_end;
    if(kui_armada_ce_vmu_runtime_services_seal(services) ||
       kui_vc2_ce_vmu_loan_bind((void *)r,
           (volatile uint32_t *)((uintptr_t)m+244u),
           (volatile uint32_t *)((uintptr_t)m+240u),
           UINT32_C(0x8c004000),UINT32_C(0x8c007ea0)))
        return KUI_VC2_VMU_GATE_OWNERSHIP;
    memset(c,0,sizeof(*c));
#endif
    return r->shared.service.ops.context==(void *)(uintptr_t)1u?
        0u:KUI_VC2_VMU_GATE_OWNERSHIP;
}

const struct retail_display_state *kui_vc2_ce_vmu_saved_display(void) {
    return vc2_saved_region?&vc2_saved_region->shared.display:NULL;
}

void kui_vc2_ce_vmu_service_snapshot(uint32_t out[16]) {
    const kui_retail_async_region_type *r=vc2_saved_region;
    for(unsigned i=0u;i<16u;++i)out[i]=0u;
    if(!r)return;
    const struct kui_retail_gd *s=&r->shared.service;
    out[0]=s->token;out[1]=s->command;out[2]=s->area;out[3]=s->request_bytes;
    out[4]=s->completed_bytes;out[5]=s->outputs[0];out[6]=s->error;
    out[7]=(uint32_t)s->status;out[8]=s->pending;out[9]=r->engine.active;
    out[10]=r->engine.opened;out[11]=r->shared.port.acquired;
    out[12]=r->shared.port.fault;out[13]=r->shared.stream.state;
    out[14]=s->initialized;out[15]=(uint32_t)(uintptr_t)r;
}
#if KUI_VC2_TEXTURE_PROBE
void kui_vc2_ce_vmu_texture_context(uint32_t out[16]) {
    /* This callback runs INSIDE the low call, before its gateway publishes
     * dirty P1 service fields to P2. Read that current cached view; the saved
     * P2 view is appropriate for the older post-call/Maple diagnostics only. */
    const kui_retail_async_region_type *r=(const void *)
        ((uintptr_t)vc2_saved_region&~(uintptr_t)UINT32_C(0x20000000));
    for(unsigned i=0u;i<16u;++i)out[i]=0u;
    if(!r)return;
    const struct kui_retail_gd *s=&r->shared.service;
    const volatile uint32_t *trace=(const volatile void *)
        ((uintptr_t)r->engine.manifest+offsetof(struct kui_retail_manifest,title));
    out[0]=s->token;out[1]=s->command;out[2]=s->lba;out[3]=s->count;
    out[4]=s->destination;out[5]=s->request_bytes;out[6]=s->completed_bytes;
    out[7]=s->outputs[0];out[8]=s->error;out[9]=s->status;
    out[10]=s->pending;out[11]=s->cpu_mmu;
    out[12]=trace[5];out[13]=trace[6];out[14]=trace[4];out[15]=trace[2];
}
#endif
