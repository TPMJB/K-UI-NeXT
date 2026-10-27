/* SPDX-License-Identifier: GPL-3.0-only */
/* Real native reader C against the existing byte/card/DMA model. SDK callback
 * assembly and original VBR restore are executed separately by SH tests. */
#include <assert.h>
#include <stdint.h>
#include "kui/native_sdk_sci_runtime.h"
static uint32_t clock_counter=UINT32_C(0x70000000),eri_slot,rxi_slot;
static unsigned slot_reads;
void kui_retail_sdk_sci_entry(void) {}
uint32_t kui_retail_async_test_read32(uint32_t address) {
    if(address==UINT32_C(0xffd8000c)) {
        uint32_t result=clock_counter;clock_counter-=19u;return result;
    }
    ++slot_reads;
    if(address==KUI_NATIVE_SDK_SCI_OWNER_VBR+KUI_NATIVE_SDK_SCI_ERI_SLOT_OFFSET)return eri_slot;
    assert(address==KUI_NATIVE_SDK_SCI_OWNER_VBR+KUI_NATIVE_SDK_SCI_RXI_SLOT_OFFSET);
    return rxi_slot;
}
volatile uint32_t kui_retail_hook_active,kui_retail_hook_fault;
#define main original_async_main
#include "test_retail_async.c"
#undef main

static uint32_t callback(void) {return (uint32_t)(uintptr_t)kui_retail_sdk_sci_entry;}
static void setup_sdk(unsigned how,bool ready) {
    cooked_fixture=true;setup(KUI_NATIVE_SDK_SCI_OWNER_VBR,how,48u,false);mode(2048u);
    kui_retail_hook_active=kui_retail_hook_fault=0u;slot_reads=0u;
    R.vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]=UINT32_C(0x8c1add0c);
    R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]=KUI_NATIVE_SDK_SCI_OWNER_VBR;
    eri_slot=rxi_slot=callback();
    if(ready)R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]=KUI_NATIVE_SDK_SCI_COOKIE;
}
static void complete(int32_t token) {
    CHECK(finish(token,0u,1u)==KUI_GD_COMPLETED);
    compare(45001u,64u,OUTPUT);
    CHECK(!R.engine.hooked && !R.engine.release.armed && !hw.vbr_sets);
}
static void bootstrap_foreground(unsigned how) {
    setup_sdk(how,false);hw.iprb=0x5a9fu;
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(R.engine.active && R.engine.hooked && !R.engine.release.armed);
    CHECK(hw.iprb==0x5a0fu && !slot_reads && hw.vbr==KUI_NATIVE_SDK_SCI_OWNER_VBR);
    unsigned before=R.engine.stats.call_blocks;
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_PROCESSING);
    CHECK(R.engine.stats.call_blocks-before==(how==Y?25u:20u));
    complete(token);CHECK(hw.iprb==0x5a9fu);
}
static void owned_interrupts(unsigned how) {
    setup_sdk(how,true);
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(R.engine.hooked && R.engine.release.armed==0x10u && hw.iprb==0x5a1fu);
    for(unsigned i=0u;i<6u;i++) {
        m.delay=0u;elapse();unsigned before=R.engine.stats.irq_blocks;
        CHECK(kui_retail_sdk_sci_dispatch(i&1u?0x500u:0x4e0u)==0u);
        CHECK(R.engine.stats.irq_blocks-before==KUI_RETAIL_NATIVE_IRQ_BATCH_LIMIT);
        CHECK(hw.vbr==KUI_NATIVE_SDK_SCI_OWNER_VBR && !hw.vbr_sets);
    }
    complete(token);CHECK(hw.iprb==0x5a0fu);
}
static void original_owner_fields(void) {
    for(unsigned ready=0u;ready<2u;ready++)for(unsigned field=0u;field<0x100u;field+=0x10u) {
        setup_sdk(X,ready!=0u);uint16_t original=(uint16_t)(0x5a0fu|field);hw.iprb=original;
        int32_t token=request(45001u,64u,OUTPUT);
        CHECK(R.engine.release.sci==field && R.engine.release.armed==(ready?0x10u:0u));
        complete(token);CHECK(hw.iprb==original);
    }
}
static void owner_field_changes(void) {
    for(unsigned field=0u;field<0x100u;field+=0x10u) {
        setup_sdk(X,true);hw.iprb=0x5a9fu;
        int32_t token=request(45001u,64u,OUTPUT);
        hw.iprb=(uint16_t)(0x5a0fu|field);
        (void)gd(KUI_GD_DRIVE,STATUS,0u);
        unsigned owner=field==0x10u?0x90u:field;
        CHECK(hw.iprb==0x5a1fu && R.engine.release.sci==owner);
        complete(token);CHECK(hw.iprb==(uint16_t)(0x5a0fu|owner));
    }
}
static void lost_route(unsigned cause) {
    setup_sdk(X,true);hw.iprb=0x5a9fu;
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(hw.iprb==0x5a1fu && R.engine.release.sci==0x90u);
    switch(cause) {
    case 0u:R.vector600[KUI_NATIVE_SDK_SCI_COOKIE_INDEX]^=1u;break;
    case 1u:R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]^=4u;break;
    case 2u:hw.vbr^=4u;break;
    case 3u:eri_slot^=4u;break;
    default:rxi_slot^=4u;break;
    }
    unsigned reads_before=slot_reads;
    CHECK(kui_retail_sdk_sci_dispatch(0x4e0u)==UINT32_C(0x8c1add0c));
    CHECK(hw.iprb==0x5a0fu && !R.engine.release.armed && R.engine.release.sci==0x90u);
    if(cause<3u)CHECK(slot_reads==reads_before);
    (void)gd(KUI_GD_DRIVE,STATUS,0u);
    CHECK(hw.iprb==0x5a0fu && R.engine.release.sci==0x90u);
    complete(token);CHECK(hw.iprb==0x5a9fu);
}
static void unowned_and_busy(void) {
    setup_sdk(X,true);
    CHECK(kui_retail_sdk_sci_dispatch(0x4e0u)==UINT32_C(0x8c1add0c));
    int32_t token=request(45001u,64u,OUTPUT);
    unsigned before=R.engine.stats.exec_int,progress=R.engine.cursor.done;
    CHECK(kui_retail_sdk_sci_dispatch(0x420u)==UINT32_C(0x8c1add0c));
    kui_retail_hook_active=1u;
    CHECK(kui_retail_sdk_sci_dispatch(0x4e0u)==UINT32_C(0x8c1add0c));
    kui_retail_hook_active=0u;R.engine.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_WRITER;
    CHECK(kui_retail_sdk_sci_dispatch(0x500u)==UINT32_C(0x8c1add0c));
    CHECK(R.engine.stats.exec_int==before && R.engine.cursor.done==progress);
    R.engine.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_IDLE;
    complete(token);
}
#ifndef KUI_NATIVE_SDK_SCI_MAIN
#define KUI_NATIVE_SDK_SCI_MAIN main
#endif
int KUI_NATIVE_SDK_SCI_MAIN(void) {
    bootstrap_foreground(X);bootstrap_foreground(Y);owned_interrupts(X);owned_interrupts(Y);
    original_owner_fields();owner_field_changes();
    for(unsigned cause=0u;cause<5u;cause++)lost_route(cause);
    unowned_and_busy();
    printf("native SDK SCI route: %u checks; bootstrapSCI0, readySCI1, all16ownerfields, lostroute masks, IRQdata, busy/loan/unowned forwarding and noVBRwrites passed\n",checks);
    return 0;
}
