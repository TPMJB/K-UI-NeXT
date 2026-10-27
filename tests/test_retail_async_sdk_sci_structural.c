/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the existing real reader/card/DMA suite with relocated SDK VBRs.
 * No game-image discovery is stubbed here: that independent pure scanner
 * and the linked cold binder have their own admission tests. */
#include "kui/native_sdk_sci_runtime.h"
#if !KUI_NATIVE_SDK_SCI_STRUCTURAL
#error "This test requires the structural SDK SCI runtime"
#endif
static uint32_t structural_vbr;
/* The shared fixture derives its simulated callback slots from this owner.
 * Production retail_async.c is compiled separately with its real header. */
#undef KUI_NATIVE_SDK_SCI_OWNER_VBR
#define KUI_NATIVE_SDK_SCI_OWNER_VBR structural_vbr
#define KUI_NATIVE_SDK_SCI_MAIN exact_sdk_suite_main
#define kui_sci_sd_acquire fixture_sci_sd_acquire
#include "test_retail_async_sdk_sci_route.c"
#undef kui_sci_sd_acquire

uint32_t kui_retail_async_vmu_test_sr(void) {return 0xf0u;}
enum kui_loader_sd_result kui_sci_sd_acquire(void) {
    enum kui_loader_sd_result result=fixture_sci_sd_acquire();
    if(result==KUI_LOADER_SD_OK)R.shared.port.acquired=true;
    return result;
}
void kui_sci_sd_release(void) {
    CHECK(kui_retail_hook_active==1u && R.shared.port.acquired &&
        !kui_sci_stream_busy() && !R.shared.stream.state &&
        !R.engine.hooked && !R.engine.release.armed);
    R.shared.port.acquired=false;
}

static void invalid_owner_before_dereference(uint32_t owner) {
    structural_vbr=owner;setup_sdk(X,true);
    CHECK(!kui_native_sdk_sci_vbr_address(owner));
    hw.iprb=0x5a9fu;
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(!slot_reads && hw.iprb==0x5a0fu && R.engine.release.armed==0u);
    CHECK(kui_retail_sdk_sci_dispatch(0x4e0u)==UINT32_C(0x8c1add0c));
    CHECK(!slot_reads && !hw.vbr_sets && hw.vbr==owner);
    complete(token);CHECK(hw.iprb==0x5a9fu);
}
static void foreign_owner_before_dereference(void) {
    structural_vbr=UINT32_C(0x8c200000);setup_sdk(X,true);
    hw.vbr=UINT32_C(0x8c300000);
    int32_t token=request(45001u,64u,OUTPUT);
    CHECK(!slot_reads && hw.iprb==0x5a0fu && !R.engine.release.armed);
    CHECK(kui_retail_sdk_sci_dispatch(0x500u)==UINT32_C(0x8c1add0c));
    CHECK(!slot_reads && hw.vbr==UINT32_C(0x8c300000) && !hw.vbr_sets);
    complete(token);
}
static void raw_owner_default_forwarding(void) {
    structural_vbr=UINT32_C(0x8c00f400);setup_sdk(X,true);
    R.vector600[KUI_NATIVE_SDK_SCI_DEFAULT_INDEX]=UINT32_C(0x0c2d8dbc);
    CHECK(kui_retail_sdk_sci_dispatch(0x4e0u)==UINT32_C(0x0c2d8dbc));
    int32_t token=request(45001u,64u,OUTPUT);
    kui_retail_hook_active=1u;
    CHECK(kui_retail_sdk_sci_dispatch(0x500u)==UINT32_C(0x0c2d8dbc));
    CHECK(kui_retail_sdk_sci_dispatch(0x420u)==UINT32_C(0x0c2d8dbc));
    kui_retail_hook_active=0u;complete(token);
}
static void loan_owner_lifetime(void) {
    for(unsigned cause=0u;cause<3u;cause++) {
        structural_vbr=UINT32_C(0x8c200000);setup_sdk(X,true);
        int32_t token=request(45001u,64u,OUTPUT);complete(token);
        struct kui_loader_sd copied;
        if(cause==1u)hw.vbr^=UINT32_C(0x100);
        enum kui_vmu_native_async_loan_result begin=kui_retail_native_async_loan_begin(&copied);
        if(cause==1u) {
            CHECK(begin==KUI_VMU_NATIVE_ASYNC_LOAN_FAULT && kui_retail_hook_fault &&
                R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_QUARANTINED);
            continue;
        }
        CHECK(begin==KUI_VMU_NATIVE_ASYNC_LOAN_OK &&
            R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_WRITER);
        if(cause==2u)hw.vbr^=UINT32_C(0x100);
        enum kui_vmu_native_async_loan_result end=kui_retail_native_async_loan_end(true);
        CHECK(end==(cause==2u?KUI_VMU_NATIVE_ASYNC_LOAN_FAULT:KUI_VMU_NATIVE_ASYNC_LOAN_OK));
        CHECK(!kui_retail_hook_active && !hw.vbr_sets &&
            R.engine.native_loan_state==(cause==2u?KUI_VMU_NATIVE_ASYNC_LOAN_QUARANTINED:
                KUI_VMU_NATIVE_ASYNC_LOAN_IDLE));
    }
}
int main(void) {
    static const uint32_t owners[]={UINT32_C(0x8c000000),UINT32_C(0x8c00f400),
        UINT32_C(0x8c010000),UINT32_C(0x8c200000),UINT32_C(0x8cfff900)};
    for(unsigned i=0u;i<sizeof(owners)/sizeof(owners[0]);i++) {
        structural_vbr=owners[i];CHECK(kui_native_sdk_sci_vbr_address(structural_vbr));
        CHECK(exact_sdk_suite_main()==0);
    }
    /* Both aliases, the first forbidden high page, wrapped/non-RAM owners
     * and every low-byte misalignment must fail before callback reads. */
    static const uint32_t bad[]={0u,UINT32_C(0x0c00f400),UINT32_C(0xac00f400),
        UINT32_C(0x8bffff00),UINT32_C(0x8cfffa00),UINT32_C(0x8cffff00),
        UINT32_C(0x8d000000),UINT32_MAX};
    for(unsigned i=0u;i<sizeof(bad)/sizeof(bad[0]);i++)invalid_owner_before_dereference(bad[i]);
    for(unsigned i=1u;i<256u;i++)invalid_owner_before_dereference(UINT32_C(0x8c00f400)+i);
    foreign_owner_before_dereference();
    raw_owner_default_forwarding();
    loan_owner_lifetime();
    printf("structural SDK SCI runtime: %u checks; five relocated owners, both readers, all16SCI owner fields, lost-route guards, 263 malformed owners before MMIO, no VBR writes passed\n",checks);
    return 0;
}
