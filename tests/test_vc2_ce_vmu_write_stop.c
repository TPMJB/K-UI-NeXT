/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual terminal-selection code around a real staged write.
 * Only the source-specific current-process word is mapped; no Maple, cache,
 * SCI or CE consumer callback is entered by this authored host boundary. */
#define _GNU_SOURCE
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include "../src/loader/armada_ce_maple_traffic.c"

struct kui_armada_ce_maple_traffic_snapshot kui_armada_ce_maple_hook_log;
struct kui_armada_ce_maple_fence_snapshot kui_armada_ce_maple_fence_log;
struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;
volatile uint32_t kui_armada_ce_maple_traffic_freeze;
volatile uint32_t kui_armada_ce_maple_proof_initial,kui_armada_ce_maple_proof_final;
const volatile uint32_t kui_armada_ce_maple_diagnostic_mode=0u;
uint32_t kui_vc2_ce_vmu_owner_context[5],kui_vc2_ce_vmu_call_context[8];
static jmp_buf terminal;
static uint32_t stopped_reason,stopped_phase;
static unsigned stop_calls,returned;

void kui_vc2_ce_vmu_diagnostic_stop(uint32_t reason,uint32_t phase) {
    ++stop_calls;stopped_reason=reason;stopped_phase=phase;
    longjmp(terminal,1);
}
static void fixture(void) {
    memset(&a2_backend,0,sizeof(a2_backend));memset(&traffic_scratch,0,sizeof(traffic_scratch));
    memset(&fence_state,0,sizeof(fence_state));memset(&a2_context,0,sizeof(a2_context));
    memset(&kui_armada_ce_maple_hook_log,0,sizeof(kui_armada_ce_maple_hook_log));
    memset(&kui_armada_ce_maple_fence_log,0,sizeof(kui_armada_ce_maple_fence_log));
    memset(&kui_armada_ce_vmu_a2_log,0,sizeof(kui_armada_ce_vmu_a2_log));
    memset(kui_vc2_ce_vmu_owner_context,0,sizeof(kui_vc2_ce_vmu_owner_context));
    fence_state.extension=UINT32_C(0x00030a60);
    traffic_scratch.map[0].virtual_address=UINT32_C(0x00030a60);
    traffic_scratch.map[0].physical_address=UINT32_C(0x8cf76a60);
    a2_context.records=5u;stop_calls=returned=0u;stopped_reason=stopped_phase=UINT32_MAX;
    *(volatile uint32_t *)(uintptr_t)KUI_CE_MAPLE_CURRENT_PROCESS=UINT32_C(0x8c1de884);
}
static void owner_checked(void) {
    const uint32_t expected[5]={UINT32_C(0x00030a60),UINT32_C(0x00030a60),
        UINT32_C(0x8cf76a60),UINT32_C(0x8c1de884),5u};
    assert(!memcmp(kui_vc2_ce_vmu_owner_context,expected,sizeof(expected)));
}
static void expect_stop(unsigned phase,unsigned reason) {
    if(!setjmp(terminal)) {
        a2_auto_capture(phase);
        /* Any terminal candidate returning here would permit the original
         * consumer to run. It must instead transfer once to diagnostics. */
        ++returned;
    }
    assert(!returned && stop_calls==1u && stopped_reason==reason && stopped_phase==phase);
    owner_checked();
}
static void pending_transaction(void) {
    uint8_t request[140],reply[524];size_t produced;
    assert(!kui_vc2_ce_vmu_write_probe_init(&a2_backend));
    for(unsigned phase=0u;phase<4u;++phase) {
        memset(request,0,sizeof(request));request[0]=12u;request[1]=2u;request[3]=34u;
        request[7]=2u;request[9]=(uint8_t)phase;request[11]=42u;
        for(unsigned i=0u;i<128u;++i)request[12u+i]=(uint8_t)(phase*128u+i);
        assert(kui_vc2_ce_vmu_write_probe_handle(&a2_backend,request,sizeof(request),
            reply,sizeof(reply),&produced)==1 && produced==4u);
    }
    memset(request,0,sizeof(request));request[0]=13u;request[1]=2u;request[3]=2u;
    request[7]=2u;request[9]=4u;request[11]=42u;
    assert(kui_vc2_ce_vmu_write_probe_handle(&a2_backend,request,12u,reply,sizeof(reply),
        &produced)==KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED && !produced);
    assert(kui_vc2_ce_vmu_write_probe_pending(&a2_backend));
}
int main(void) {
    const uintptr_t page=KUI_CE_MAPLE_CURRENT_PROCESS&~(uintptr_t)4095u;
    assert(mmap((void *)page,4096u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)page);
    fixture();
    a2_auto_capture(KUI_ARMADA_CE_MAPLE_AUTO_BEFORE_ORIGINAL);
    a2_auto_capture(KUI_ARMADA_CE_MAPLE_AUTO_AFTER_ORIGINAL);
    assert(!stop_calls);
    kui_armada_ce_vmu_a2_log.counts[3]=1u;
    kui_armada_ce_vmu_a2_log.commands[0]=1u;
    kui_armada_ce_vmu_a2_log.commands[3]=15u;
    kui_armada_ce_vmu_a2_log.commands[4]=1u;
    a2_auto_capture(KUI_ARMADA_CE_MAPLE_AUTO_AFTER_ORIGINAL);
    assert(!stop_calls); /* Old read/finalization success is not this checkpoint. */
    fixture();pending_transaction();
    expect_stop(KUI_ARMADA_CE_MAPLE_AUTO_BEFORE_ORIGINAL,7u);
    assert(a2_backend.commit_attempts==1u && !a2_backend.endpoint.commits);
    fixture();pending_transaction();kui_armada_ce_vmu_a2_log.fault[0]=96u;
    expect_stop(KUI_ARMADA_CE_MAPLE_AUTO_BEFORE_ORIGINAL,KUI_ARMADA_CE_MAPLE_AUTO_FAULT);
    fixture();kui_armada_ce_maple_hook_log.fault[0]=1u;
    expect_stop(KUI_ARMADA_CE_MAPLE_AUTO_BEFORE_ORIGINAL,KUI_ARMADA_CE_MAPLE_AUTO_FAULT);
    fixture();kui_armada_ce_maple_fence_log.fault[0]=1u;
    expect_stop(KUI_ARMADA_CE_MAPLE_AUTO_BEFORE_ORIGINAL,KUI_ARMADA_CE_MAPLE_AUTO_FAULT);
    fixture();kui_armada_ce_maple_hook_log.counts[1]=KUI_ARMADA_CE_MAPLE_AUTO_LIMIT;
    a2_auto_capture(KUI_ARMADA_CE_MAPLE_AUTO_BEFORE_ORIGINAL);assert(!stop_calls);
    expect_stop(KUI_ARMADA_CE_MAPLE_AUTO_AFTER_ORIGINAL,KUI_ARMADA_CE_MAPLE_AUTO_VISIT_LIMIT);
    assert(munmap((void *)page,4096u)==0);
    puts("PASS actual VC2 terminal selection: unready remains live, complete phase4 "
        "stops BEFORE despite AUTO_PHASE2, three fault sources take priority, "
        "visit limit stops POST, exact owner snapshot, no return to consumer");
    return 0;
}
