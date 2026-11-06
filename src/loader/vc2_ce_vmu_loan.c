/* SPDX-License-Identifier: GPL-3.0-only */
/* Compile with the native foreground ABI flags, not KUI_RETAIL_CE. The
 * source-bound low reader stays unchanged; all new state/code is high. */
#include "kui/vc2_ce_vmu_loan.h"
#include "kui/vc2_ce_vmu_handoff.h"
#include "kui/armada_ce_vmu_layout.h"
#include "kui/vc2_ce_vmu_gd_wait.h"
#include "retail_async.h"

KUI_VC2_VMU_NATIVE_LAYOUT_ASSERTS();
#define LOAN_MAGIC UINT32_C(0x314c4356) /* VCL1 */
enum {LOAN_UNBOUND,LOAN_IDLE,LOAN_WRITER,LOAN_QUARANTINED};
enum {FAULT_BIND=1,FAULT_LOW_STATE,FAULT_LOCK,FAULT_RESTORE};
static struct {
    volatile kui_retail_async_region_type *region;
    const struct kui_retail_manifest *manifest;
    volatile uint32_t *active,*fault;
    uintptr_t begin,end;
    uint32_t magic,started,state,claims,releases,busy,first_fault,last_result;
} loan;

#ifdef KUI_VC2_VMU_LOAN_TEST
extern uint32_t kui_vc2_vmu_loan_test_sr(void);
extern void kui_vc2_vmu_loan_test_set_sr(uint32_t);
extern void kui_vc2_vmu_loan_test_publish(uintptr_t,uintptr_t);
extern void kui_vc2_vmu_loan_test_invalidate(uintptr_t);
#define sr_get kui_vc2_vmu_loan_test_sr
#define sr_set kui_vc2_vmu_loan_test_set_sr
#define publish kui_vc2_vmu_loan_test_publish
#define invalidate kui_vc2_vmu_loan_test_invalidate
#else
static uint32_t sr_get(void) {
    uint32_t value;__asm__ volatile("stc sr,%0":"=r"(value));return value;
}
static void sr_set(uint32_t value) {
    __asm__ volatile("ldc %0,sr"::"r"(value):"t","memory");
}
static void publish(uintptr_t begin,uintptr_t end) {
    for(uintptr_t at=begin&~(uintptr_t)31u;at<end;at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
}
static void invalidate(uintptr_t at) {
    at&=~(uintptr_t)31u;
    __asm__ volatile("ocbi @%0"::"r"(at):"memory");
}
#endif
static bool span(const void *p,size_t bytes) {
    return p && bytes<=UINTPTR_MAX-(uintptr_t)p;
}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an && bn && x<y+bn && y<x+an;
}
static uintptr_t p1(uintptr_t at) {
#ifndef KUI_VC2_VMU_LOAN_TEST
    at^=UINT32_C(0x20000000);
#endif
    return at;
}
static uintptr_t p2(uintptr_t at) {
#ifndef KUI_VC2_VMU_LOAN_TEST
    at|=UINT32_C(0x20000000);
#endif
    return at;
}
static void increment(uint32_t *v) {if(*v!=UINT32_MAX)++*v;}
static bool bound(void) {
    return loan.started==1u && loan.magic==LOAN_MAGIC && loan.region &&
        loan.active && loan.fault && loan.begin<loan.end;
}
static int result(int value) {loan.last_result=(uint32_t)value;return value;}
static int quarantine(unsigned why,bool owns_lock) {
    if(!loan.first_fault)loan.first_fault=why;
    loan.state=LOAN_QUARANTINED;
    if(bound()) {
        volatile kui_retail_async_region_type *r=loan.region;
        r->shared.port.fault=true;r->shared.card.device.sd.ready=false;
        r->engine.failed=KUI_GD_ERROR_IO;*loan.fault=1u;
        if(owns_lock && *loan.active==KUI_VC2_CE_VMU_GD_LOAN_FLAG)*loan.active=0u;
        /* The whole range was published before these P2 stores. No dirty
         * native alias may subsequently restore an apparently healthy card. */
        for(uintptr_t at=loan.begin&~(uintptr_t)31u;at<loan.end;at+=32u)invalidate(at);
    }
    return result(2);
}
int kui_vc2_ce_vmu_loan_bind(void *region,volatile uint32_t *active,
    volatile uint32_t *fault,uintptr_t begin,uintptr_t end) {
    if(loan.started)return -1;
    loan.started=1u;
    if((sr_get()&UINT32_C(0xf0))!=UINT32_C(0xf0) ||
       !span(region,sizeof(kui_retail_async_region_type)) || !span((const void *)active,4u) ||
       !span((const void *)fault,4u) || ((uintptr_t)region&31u) ||
       ((uintptr_t)active&3u) || ((uintptr_t)fault&3u) ||
       begin>=end || (begin&31u) || (end&3u))goto refused;
    uintptr_t address=(uintptr_t)region;
#ifndef KUI_VC2_VMU_LOAN_TEST
    if((address&UINT32_C(0xe000001f))!=UINT32_C(0xa0000000) ||
       address<UINT32_C(0xac004000) || address>UINT32_C(0xac007ea0)-sizeof(kui_retail_async_region_type) ||
       begin<UINT32_C(0x8c004000) || end>UINT32_C(0x8c007ea0))goto refused;
#endif
    if(p1(address)<begin || p1(address)>end ||
       sizeof(kui_retail_async_region_type)>end-p1(address))goto refused;
    publish(begin,end);
    volatile kui_retail_async_region_type *r=region;
    uintptr_t source=(uintptr_t)r->engine.manifest;
#ifndef KUI_VC2_VMU_LOAN_TEST
    if((source&UINT32_C(0xe0000003))!=UINT32_C(0x80000000))goto refused;
#endif
    uintptr_t map=p2(source);
    if(!source || (source&3u) || map>UINTPTR_MAX-sizeof(struct kui_retail_manifest) ||
       (uintptr_t)active!=map+244u || (uintptr_t)fault!=map+240u ||
       p1(map)<begin || p1(map)>end || sizeof(struct kui_retail_manifest)>end-p1(map) ||
       (uintptr_t)r->shared.service.tracks!=(uintptr_t)r->engine.manifest+
           offsetof(struct kui_retail_manifest,slots) ||
       (uintptr_t)r->shared.service.ops.context!=1u)goto refused;
    loan.region=r;loan.manifest=(const void *)map;
    loan.active=active;loan.fault=fault;loan.begin=begin;loan.end=end;
    loan.magic=LOAN_MAGIC;loan.state=LOAN_IDLE;return 0;
refused:
    loan.first_fault=FAULT_BIND;loan.state=LOAN_QUARANTINED;return -1;
}
static bool idle(void) {
    volatile kui_retail_async_region_type *r=loan.region;
    volatile struct kui_retail_gd *s=&r->shared.service;
    return s->initialized && !s->pending && !s->executing && !s->callback_due &&
        !r->engine.active && !r->engine.in_irq && !r->engine.opened && !r->engine.hooked &&
        !r->engine.release.armed && !r->engine.release.depth &&
        !r->shared.port.acquired && !r->shared.stream.state && !r->shared.card.stream.active;
}
static bool healthy(void) {
    volatile kui_retail_async_region_type *r=loan.region;
    const struct kui_retail_manifest *m=loan.manifest;
    return p2((uintptr_t)r->engine.manifest)==(uintptr_t)m &&
        (uintptr_t)r->shared.service.tracks==(uintptr_t)r->engine.manifest+
            offsetof(struct kui_retail_manifest,slots) &&
        (uintptr_t)r->shared.service.ops.context==1u &&
        !*loan.fault && !r->shared.port.fault && !r->engine.failed &&
        r->shared.card.transport==KUI_STORAGE_SCI && m->storage_transport==KUI_STORAGE_SCI &&
        r->shared.card.device.sd.ready && !r->shared.card.device.sd.slow &&
        r->shared.card.device.sd.blocks && r->shared.card.device.sd.blocks<=UINT64_C(0x100000000) &&
        r->shared.card.device.sd.blocks==m->card_sectors;
}
int kui_vc2_ce_vmu_loan_begin(struct kui_loader_sd *out) {
    uint32_t saved=sr_get();
    if((saved&UINT32_C(0x500000f0))!=UINT32_C(0x40000000))return result(3);
    if(!bound() || loan.state==LOAN_QUARANTINED || !span(out,sizeof(*out)) ||
       ((uintptr_t)out%_Alignof(struct kui_loader_sd)))return result(2);
    if(overlap(out,sizeof(*out),&loan,sizeof(loan)) ||
       overlap(out,sizeof(*out),(const void *)p2(loan.begin),loan.end-loan.begin))return result(2);
#ifndef KUI_VC2_VMU_LOAN_TEST
    if((uintptr_t)out<KUI_ARMADA_CE_VMU_WORKER_STACK_BOTTOM ||
       (uintptr_t)out>KUI_ARMADA_CE_VMU_WORKER_STACK_TOP-sizeof(*out))return result(2);
#endif
    sr_set(saved|UINT32_C(0xf0));
    publish(loan.begin,loan.end);
    if(loan.state!=LOAN_IDLE || *loan.active || !idle()) {
        increment(&loan.busy);sr_set(saved);return result(1);
    }
    if(!healthy()) {int value=quarantine(FAULT_LOW_STATE,false);sr_set(saved);return value;}
    *loan.active=KUI_VC2_CE_VMU_GD_LOAN_FLAG;
    invalidate(p1((uintptr_t)loan.active));
    loan.state=LOAN_WRITER;increment(&loan.claims);
    *out=loan.region->shared.card.device.sd;
    sr_set(saved);return result(0);
}
int kui_vc2_ce_vmu_loan_end(bool restored) {
    uint32_t saved=sr_get();
    if((saved&UINT32_C(0x500000f0))!=UINT32_C(0x40000000))return result(3);
    if(!bound() || loan.state==LOAN_QUARANTINED)return result(2);
    if(loan.state!=LOAN_WRITER) {
        if(!loan.first_fault)loan.first_fault=FAULT_LOCK;
        loan.state=LOAN_QUARANTINED;return result(2);
    }
    sr_set(saved|UINT32_C(0xf0));
    /* Waiting GD entries can have updated neighboring diagnostic words while
     * observing the dedicated loan flag. Publish before clearing its P2 word. */
    publish(loan.begin,loan.end);
    if(*loan.active!=KUI_VC2_CE_VMU_GD_LOAN_FLAG || !restored || !idle() || !healthy()) {
        int value=quarantine(restored?FAULT_LOCK:FAULT_RESTORE,loan.state==LOAN_WRITER);
        sr_set(saved);return value;
    }
    loan.state=LOAN_IDLE;increment(&loan.releases);
    *loan.active=0u;invalidate(p1((uintptr_t)loan.active));
    sr_set(saved);return result(0);
}
void kui_vc2_ce_vmu_loan_snapshot(uint32_t out[8]) {
    if(!span(out,8u*4u) || ((uintptr_t)out&3u) ||
       overlap(out,8u*4u,&loan,sizeof(loan)))return;
    out[0]=loan.magic;out[1]=loan.state;out[2]=loan.claims;out[3]=loan.releases;
    out[4]=loan.busy;out[5]=loan.first_fault;out[6]=(uint32_t)(uintptr_t)loan.region;
    out[7]=loan.last_result;
}
