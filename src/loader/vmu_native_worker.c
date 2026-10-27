/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_worker.h"
#include "kui/vmu_readonly.h"
#include "kui/hash.h"
#include <string.h>
extern const struct kui_vmu_native_worker_exports kui_vmu_native_worker_exports;
extern uint8_t bss_begin[] __asm__("__vmu_native_worker_bss_begin");
extern uint8_t bss_end[] __asm__("__vmu_native_worker_bss_end");
extern uint32_t stack_bottom[] __asm__("__vmu_native_worker_stack_bottom");
extern uint32_t stack_top[] __asm__("__vmu_native_worker_stack_top");
static volatile uint32_t entered;
static bool worker_read(void *context,uint32_t address,uint32_t *value) {
    (void)context;uint32_t area=address&0xe0000000u,p=address&0x1fffffffu;
    if((area!=0u && area!=0x80000000u && area!=0xa0000000u) ||
       (address&3u) || p<0x0c000000u || p>0x0cfffffcu) return false;
    *value=*(volatile const uint32_t *)(uintptr_t)address;return true;
}
int kui_vmu_native_worker_probe(const struct kui_vmu_native_worker_config *c,struct kui_vmu_native_worker_result *r) {
    if(!c || !r || c->version!=1u || c->bytes!=sizeof(*c) || !c->lease ||
       c->worker_span!=0x10000u || c->lease_bytes!=0x30000u || c->initializations!=1u ||
       (c->lease_address&31u) || c->lease_address<0x8c010000u || c->lease_address>0x8cfd0000u) return -1;
    memset(r,0,sizeof(*r));r->version=1u;r->bytes=sizeof(*r);
    uint32_t sp,sr;__asm__ volatile("mov r15,%0\n\tstc sr,%1":"=r"(sp),"=r"(sr));
    r->entry_sp=sp;r->entry_sr=sr;r->self_address=(uint32_t)(uintptr_t)&kui_vmu_native_worker_exports;
    r->stack_low=(uint32_t)(uintptr_t)stack_bottom;r->stack_high=(uint32_t)(uintptr_t)stack_top;
    int result=-2;
    if(((sr>>4)&15u)!=15u || sp<r->stack_low+256u || sp>=r->stack_high) goto done;
    result=-3;uint32_t p2=c->lease_address+0x20000000u;
    if(r->self_address!=p2 || r->stack_low!=p2+0xf000u || r->stack_high!=p2+0x10000u ||
       (uint32_t)(uintptr_t)bss_begin<p2+64u || (uintptr_t)bss_end<(uintptr_t)bss_begin ||
       (uint32_t)(uintptr_t)bss_end>r->stack_low || c->lease->payload!=(c->lease_address&0x1fffffffu) ||
       c->lease->lease_bytes!=c->lease_bytes) goto done;
    result=-5;
    for(volatile uint8_t *at=bss_begin;at<bss_end;at++) if(*at) goto done;
    entered=1u;result=-4;
    if(!kui_vmu_native_heap_lease_valid(c->lease,worker_read,NULL,c->initializations)) goto done;
    r->lease_valid=1u;result=kui_vmu_native_worker_protocol_probe(r);
    if(!result && (!kui_vmu_native_heap_lease_valid(c->lease,worker_read,NULL,c->initializations) || entered!=1u)) result=-4;
    r->stack_guard=stack_bottom[0];if(!result && r->stack_guard!=UINT32_C(0x4b535431)) result=-2;
done:
    r->status=(uint32_t)result;return result;
}
