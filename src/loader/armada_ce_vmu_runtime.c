/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_vmu_runtime.h"
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME || defined(KUI_ARMADA_CE_VMU_RUNTIME_TEST)
#include "kui/armada_ce_vmu_sd.h"
#include "kui/armada_ce_vmu_write.h"
#include "kui/armada_ce_vmu_a2_publish.h"
#include <string.h>

struct kui_vmu_durable_handoff kui_armada_ce_vmu_runtime_handoff
    __attribute__((section(".vmu_handoff"),used));
struct kui_armada_ce_vmu_runtime_services kui_armada_ce_vmu_runtime_services
    __attribute__((section(".vmu_services"),used));
static struct {
    struct kui_armada_ce_vmu_write endpoint;
    struct kui_vmu_deferred_io queue;
    struct kui_armada_ce_vmu_sd sd;
    uint32_t started,ready,owner,handling,worker,faulted,busy_rounds;
} runtime;

#ifdef KUI_ARMADA_CE_VMU_RUNTIME_TEST
extern uint32_t kui_armada_ce_vmu_runtime_test_sr(void);
extern void kui_armada_ce_vmu_runtime_test_set_sr(uint32_t);
extern uintptr_t kui_armada_ce_vmu_runtime_test_sp(void);
extern int kui_armada_ce_vmu_runtime_test_loan_begin(struct kui_loader_sd *);
extern int kui_armada_ce_vmu_runtime_test_loan_end(bool);
#define runtime_sr kui_armada_ce_vmu_runtime_test_sr
#define runtime_set_sr kui_armada_ce_vmu_runtime_test_set_sr
#define runtime_sp kui_armada_ce_vmu_runtime_test_sp
#else
static uint32_t runtime_sr(void) {
    uint32_t value;__asm__ volatile("stc sr,%0":"=r"(value));return value;
}
static void runtime_set_sr(uint32_t value) {
    __asm__ volatile("ldc %0,sr"::"r"(value):"t","memory");
}
static uintptr_t runtime_sp(void) {
    uintptr_t value;__asm__ volatile("mov r15,%0":"=r"(value));return value;
}
#endif
static bool runtime_guard_enter(void *context,uint32_t *saved) {
    if(context!=&runtime || !saved) return false;
    uint32_t sr=runtime_sr();
    if(!(sr&UINT32_C(0x40000000)) || (sr&UINT32_C(0x10000000))) return false;
    runtime_set_sr(sr|UINT32_C(0xf0));*saved=sr;return true;
}
static void runtime_guard_leave(void *context,uint32_t saved) {
    (void)context;runtime_set_sr(saved);
}
static bool runtime_worker_ready(void *context) {
    uint32_t sr=runtime_sr(),saved;
    uintptr_t sp=runtime_sp();
    if(context!=&runtime || !runtime.ready || !runtime.worker || runtime.handling ||
       (sr&UINT32_C(0x500000f0))!=UINT32_C(0x40000000) ||
       sp<KUI_ARMADA_CE_VMU_WORKER_STACK_BOTTOM ||
       sp>=KUI_ARMADA_CE_VMU_WORKER_STACK_TOP || !runtime_guard_enter(context,&saved)) return false;
    bool valid=kui_armada_ce_maple_runtime_owner(runtime.owner);
    runtime_guard_leave(context,saved);return valid;
}
int kui_armada_ce_vmu_runtime_bind(uint32_t owner) {
    if((runtime_sr()&UINT32_C(0x500000f0))!=UINT32_C(0x400000f0) ||
       !owner || (owner&3u) || owner>=UINT32_C(0x80000000)) return -1;
    if(runtime.started) return runtime.ready && runtime.owner==owner && !runtime.faulted?0:-1;
    runtime.started=1u;runtime.owner=owner;
    if(kui_vmu_durable_handoff_validate(&kui_armada_ce_vmu_runtime_handoff) ||
       kui_armada_ce_vmu_runtime_services_validate(&kui_armada_ce_vmu_runtime_services)) goto failed;
    const struct kui_vmu_deferred_guard guard={&runtime,runtime_guard_enter,
        runtime_guard_leave,runtime_worker_ready};
    const struct kui_armada_ce_vmu_runtime_services *services=&kui_armada_ce_vmu_runtime_services;
    const struct kui_armada_ce_vmu_sd_ops sd_ops={
#ifdef KUI_ARMADA_CE_VMU_RUNTIME_TEST
        kui_armada_ce_vmu_runtime_test_loan_begin,kui_armada_ce_vmu_runtime_test_loan_end,
#else
        (int (*)(struct kui_loader_sd *))(uintptr_t)services->loan_begin,
        (int (*)(bool))(uintptr_t)services->loan_end,
#endif
        guard};
#ifdef KUI_ARMADA_CE_VMU_RUNTIME_TEST
    (void)services;
#endif
    if(kui_armada_ce_vmu_sd_init(&runtime.sd,&kui_armada_ce_vmu_runtime_handoff,&sd_ops)) goto failed;
    const struct kui_vmu_emulation_io provider={&runtime.sd,
        kui_armada_ce_vmu_sd_read,kui_armada_ce_vmu_sd_commit};
    if(kui_vmu_deferred_io_init(&runtime.queue,&provider,&guard)) goto failed;
    const struct kui_vmu_emulation_io queue={&runtime.queue,
        kui_vmu_deferred_io_read,kui_vmu_deferred_io_commit};
    if(kui_armada_ce_vmu_write_init(&runtime.endpoint,&queue)) goto failed;
    runtime.ready=1u;return 0;
failed:
    runtime.faulted=1u;return -1;
}
int kui_armada_ce_vmu_runtime_handle(void *context,const uint8_t *request,size_t bytes,
    uint8_t *response,size_t capacity,size_t *produced) {
    (void)context;
    if(!runtime.ready || runtime.handling || runtime.worker ||
       (runtime_sr()&UINT32_C(0x500000f0))!=UINT32_C(0x400000f0)) return -1;
    runtime.handling=1u;
    int result=kui_armada_ce_vmu_write_handle(&runtime.endpoint,request,bytes,response,capacity,produced);
    runtime.handling=0u;
    if(result==KUI_ARMADA_CE_VMU_WRITE_HANDLED && *produced==4u && response[0]==0xfcu &&
       bytes==12u && (request[0]==11u || (request[0]==13u && request[9]==4u)) &&
       (runtime.queue.state==KUI_VMU_DEFERRED_PENDING || runtime.queue.state==KUI_VMU_DEFERRED_RUNNING)) {
        *produced=0u;return KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED;
    }
    return result;
}
int kui_armada_ce_vmu_runtime_work(uint32_t owner) {
    uint32_t saved;
    if(!runtime.ready || runtime.handling || runtime.worker || owner!=runtime.owner ||
       (runtime_sr()&UINT32_C(0x500000f0))!=UINT32_C(0x40000000) ||
       !runtime_guard_enter(&runtime,&saved)) return -2;
    runtime.worker=1u;runtime_guard_leave(&runtime,saved);
    if(!runtime_worker_ready(&runtime)) {
        if(runtime_guard_enter(&runtime,&saved)) {
            runtime.worker=0u;runtime_guard_leave(&runtime,saved);
        }
        return -2;
    }
    int result=kui_vmu_deferred_io_work(&runtime.queue);
    if(!runtime_guard_enter(&runtime,&saved)) return -2;
    /* BUSY promises no acquired I/O. Give CE's GD worker a bounded sequence
     * of real 1 ms waits (the shim uses the ordinary CE stack), then expose
     * a real error through fresh admission rather than spin or invent ACK.
     * DONE/RUNNING are never discarded by this pre-acquisition deadline. */
    if(!result && runtime.queue.state==KUI_VMU_DEFERRED_PENDING) {
        if(++runtime.busy_rounds>=1000u)
            result=kui_vmu_deferred_io_poison(&runtime.queue)?-2:-1;
    } else runtime.busy_rounds=0u;
    /* A real FILEERR is insufficient after an acquired storage failure:
     * the quarantined GD reader cannot finish the loading screen. Preserve
     * the provider's first cause before a fresh PRE enters terminal capture.
     * Only the existing report is written, with interrupts already masked. */
    extern struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;
    uint32_t *fault=kui_armada_ce_vmu_a2_log.fault;
    if(result<0 && !fault[0]) {
        fault[1]=(uint32_t)runtime.sd.last_result;
        fault[2]=((uint32_t)runtime.sd.last_sd_result&0xffffu)|(runtime.sd.pio.fault<<16);
        fault[3]=(uint32_t)runtime.sd.last_loan_result;
        fault[4]=runtime.sd.pio.detail;
        fault[0]=runtime.sd.faulted?112u:113u;
    }
    runtime.worker=0u;runtime_guard_leave(&runtime,saved);
    return result;
}
#endif
