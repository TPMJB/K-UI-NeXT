/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_vmu_support.h"
#include "kui/disc_vmu_transport.h"
#include "kui/vmu_native_passive.h"
#include "kui/vmu_durable.h"
#include <string.h>

#define READY UINT32_C(0x44565342)
struct support_state {
    uint32_t ready,terminal,stack_top;
    struct kui_disc_vmu_transport transport;
};
/* Assembly uses only the first three immutable-after-bind words. Mutable
 * storage/port state is P2, separated from immutable P1 code by whole lines. */
struct support_state kui_disc_vmu_support_state __attribute__((aligned(32)));

/* Keep address arithmetic at runtime. Folding self-72KiB into a DIR32 literal
 * would create an external relocation into the separately relocated worker. */
static uint32_t __attribute__((noipa)) self(void) {
#ifdef KUI_DISC_VMU_SUPPORT_TEST
    extern uint32_t kui_disc_vmu_support_test_self(void);
    return kui_disc_vmu_support_test_self();
#else
    return (uint32_t)(uintptr_t)&kui_disc_vmu_support_exports;
#endif
}
static bool worker_exports(uint32_t address,struct kui_vmu_native_passive_exports *out) {
#ifdef KUI_DISC_VMU_SUPPORT_TEST
    extern bool kui_disc_vmu_support_test_exports(uint32_t,void *,size_t);
    return kui_disc_vmu_support_test_exports(address,out,sizeof(*out));
#else
    memcpy(out,(const void *)(uintptr_t)address,sizeof(*out));return true;
#endif
}
static bool code(uint32_t address,uint32_t begin,uint32_t end) {
    return !(address&1u) && address>=begin+80u && address<end;
}
int kui_disc_vmu_support_bind(const struct kui_disc_vmu_support_request *r) {
    if(!r || r->version!=KUI_DISC_VMU_SUPPORT_VERSION || r->bytes!=sizeof(*r)) return -1;
    const uint8_t *zero=(const uint8_t *)&kui_disc_vmu_support_state;
    for(size_t i=0u;i<sizeof(kui_disc_vmu_support_state);i++) if(zero[i]) return -1;
    uint32_t support=self();
    if((support&31u) || support<UINT32_C(0xac010020)+KUI_DISC_VMU_WORKER_BYTES ||
       support>UINT32_C(0xad000000)-KUI_DISC_VMU_SUPPORT_BYTES) return -1;
    uint32_t worker=support-KUI_DISC_VMU_WORKER_BYTES,p1=worker-UINT32_C(0x20000000);
    uint32_t area=r->lease_begin&UINT32_C(0xe0000000);
    if((area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       (r->lease_begin&UINT32_C(0x1fffffff))!=(worker&UINT32_C(0x1fffffff)) ||
       r->lease_bytes!=KUI_DISC_VMU_LEASE_BYTES || r->worker_begin!=p1) return -1;
    struct kui_vmu_native_passive_exports e;
    if(!worker_exports(worker,&e) || e.magic!=KUI_VMU_NATIVE_PASSIVE_MAGIC ||
       e.version!=4u || e.bytes!=80u || e.memory_end!=support ||
       e.stack_top!=support || e.stack_bottom!=support-4096u ||
       (e.bss_begin|e.bss_end)&31u || e.bss_begin<worker+80u ||
       e.bss_end<e.bss_begin || e.bss_end>e.stack_bottom ||
       r->worker_stack_top!=e.stack_top || r->worker_transaction!=e.sd_transaction ||
       r->worker_terminal!=e.terminal ||
       !code(r->worker_transaction,p1,e.bss_begin-UINT32_C(0x20000000)) ||
       !code(r->worker_terminal,p1,e.bss_begin-UINT32_C(0x20000000)) ||
       (r->worker_context&31u) || r->worker_context<e.bss_begin ||
       r->worker_context>e.bss_end || sizeof(struct kui_vmu_durable)>e.bss_end-r->worker_context)
        return -1;
    if(!kui_disc_vmu_transport_bind(&kui_disc_vmu_support_state.transport,
       r->transport,r->cid,&r->card,
       (kui_vmu_sd_transaction)(uintptr_t)r->worker_transaction,
       (void *)(uintptr_t)r->worker_context)) return -1;
    kui_disc_vmu_support_state.terminal=e.terminal;
    kui_disc_vmu_support_state.stack_top=e.stack_top;
    __asm__ volatile("":::"memory");
    kui_disc_vmu_support_state.ready=READY;
    return 0;
}
int kui_disc_vmu_support_sd_lease(kui_vmu_sd_transaction transaction,void *context) {
    if(kui_disc_vmu_support_state.ready!=READY) return KUI_VMU_SD_LEASE_FAULT;
    return kui_disc_vmu_transport_loan(&kui_disc_vmu_support_state.transport,transaction,context);
}
