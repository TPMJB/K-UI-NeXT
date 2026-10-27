/* SPDX-License-Identifier: GPL-3.0-only */
/* Compile the actual isolated gateway body against a fake resident/bus. */
#define KUI_VMU_SD_LEASE_HOST_TEST 1
#include "kui/vmu_maple_observe_boot.h"
#include "kui/vmu_sd_lease.h"
#include "retail_storage.h"
#include "sci_sd_bus.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_retail_storage card;
volatile uint32_t kui_retail_hook_active;
struct kui_vmu_observe_boot_control kui_vmu_observe_boot_control;
static uint32_t entry_sr;
static unsigned acquisitions,releases,callbacks,wire_bytes;
static int acquisition_result,callback_result;
static bool acquired,fault,callback_fault,release_fault,loan_not_ready,reenter;
static unsigned bus_context;
#ifdef KUI_VMU_SD_LEASE_NATIVE_HOST_TEST
#include "vmu_native_resident_guards.h"
#if KUI_VMU_NATIVE_CACHED_CODE
#define TEST_CODE_ALIAS UINT32_C(0x80000000)
#define TEST_WRONG_CODE_ALIAS UINT32_C(0xa0000000)
#else
#define TEST_CODE_ALIAS UINT32_C(0xa0000000)
#define TEST_WRONG_CODE_ALIAS UINT32_C(0x80000000)
#endif
static int transaction(void *,struct kui_loader_sd *);
static uintptr_t native_target;
uintptr_t kui_vmu_sd_lease_test_target(kui_vmu_sd_transaction callback) {
    return callback==transaction?native_target:0u;
}
#endif
uint32_t kui_vmu_sd_lease_test_sr(void) {return entry_sr;}
static uint8_t transfer(void *context,uint8_t value,bool slow) {
    assert(context==&bus_context && !slow && acquired && kui_retail_hook_active==1u);
    ++wire_bytes;return value;
}
enum kui_loader_sd_result kui_retail_storage_acquire(struct kui_retail_storage *owner) {
    assert(owner==&card && kui_retail_hook_active==1u);
    ++acquisitions;
    if(!acquisition_result) acquired=true;
    return (enum kui_loader_sd_result)acquisition_result;
}
void kui_retail_storage_release(struct kui_retail_storage *owner) {
    assert(owner==&card && kui_retail_hook_active==1u && acquired);
    ++releases;acquired=false;if(release_fault) fault=true;
}
bool kui_sci_sd_healthy(void) {return acquired && !fault;}
bool kui_sci_sd_faulted(void) {return fault;}

#include "../src/loader/vmu_sd_lease.inc"

static int transaction(void *context,struct kui_loader_sd *loan) {
    assert(context==&bus_context && loan!=&card.device.sd);
    assert(acquired && kui_retail_hook_active==1u && card.device.sd.ready);
    assert(loan->bus.ctx==card.device.sd.bus.ctx && loan->bus.transfer==transfer);
    assert(loan->blocks==card.device.sd.blocks && loan->high_capacity==card.device.sd.high_capacity);
    ++callbacks;
    if(reenter) {
        unsigned before=acquisitions;
        assert(kui_vmu_observe_sd_lease(transaction,context)==KUI_VMU_SD_LEASE_BUSY);
        assert(acquisitions==before && acquired && kui_retail_hook_active==1u);
    }
    (void)loan->bus.transfer(loan->bus.ctx,0xff,false);
    loan->last_command=17u;loan->last_response=0u;
    if(loan_not_ready) loan->ready=false;
    if(callback_fault) fault=true;
    return callback_result;
}
static int other_transaction(void *context,struct kui_loader_sd *loan) {
    (void)context;(void)loan;assert(!"Unpinned callback executed");return -1;
}
static void fresh(void) {
    memset(&card,0,sizeof(card));
    card.transport=KUI_STORAGE_SCI;card.device.sd.ready=true;
    card.device.sd.blocks=123456u;card.device.sd.high_capacity=true;
    card.device.sd.last_command=9u;card.device.sd.last_response=0x5au;
    card.device.sd.bus.ctx=&bus_context;card.device.sd.bus.transfer=transfer;
    kui_vmu_observe_boot_control=(struct kui_vmu_observe_boot_control){
        .status=KUI_VMU_OBS_BOOT_INSTALLED,.sd_transaction=(uint32_t)(uintptr_t)transaction};
#ifdef KUI_VMU_SD_LEASE_NATIVE_HOST_TEST
    native_target=UINT32_C(0x0c38a980)|TEST_CODE_ALIAS;
    kui_vmu_observe_boot_control.lease=UINT32_C(0x0c38a880);
    kui_vmu_observe_boot_control.worker_end=UINT32_C(0xac3aa880);
    kui_vmu_observe_boot_control.terminal=UINT32_C(0x0c38aa80)|TEST_CODE_ALIAS;
    kui_vmu_observe_boot_control.sd_transaction=(uint32_t)native_target;
#endif
    entry_sr=UINT32_C(0x600000f3);kui_retail_hook_active=0;
    acquisitions=releases=callbacks=wire_bytes=0;
    acquisition_result=callback_result=0;
    acquired=fault=callback_fault=release_fault=loan_not_ready=reenter=false;
}
static int call(void) {return kui_vmu_observe_sd_lease(transaction,&bus_context);}
static void no_io(void) {assert(!acquisitions && !releases && !callbacks && !wire_bytes);}
static void successful_borrow_and_reentry(void) {
    fresh();struct kui_loader_sd before=card.device.sd;reenter=true;
    assert(call()==KUI_VMU_SD_LEASE_OK);
    assert(acquisitions==1u && releases==1u && callbacks==1u && wire_bytes==1u);
    assert(!memcmp(&before,&card.device.sd,sizeof(before)));
    assert(!kui_retail_hook_active && !acquired && entry_sr==UINT32_C(0x600000f3));
    assert(call()==KUI_VMU_SD_LEASE_OK && acquisitions==2u && releases==2u);
}
static void reject_before_io(void) {
    fresh();entry_sr&=~UINT32_C(0x10);assert(call()==KUI_VMU_SD_LEASE_FAULT);no_io();
    fresh();assert(kui_vmu_observe_sd_lease(NULL,NULL)==KUI_VMU_SD_LEASE_FAULT);no_io();
    fresh();assert(kui_vmu_observe_sd_lease(other_transaction,NULL)==KUI_VMU_SD_LEASE_FAULT);no_io();
    fresh();kui_vmu_observe_boot_control.status=KUI_VMU_OBS_BOOT_LEASED;
    assert(call()==KUI_VMU_SD_LEASE_FAULT);no_io();
    fresh();kui_vmu_observe_boot_control.sd_transaction=0;
    assert(call()==KUI_VMU_SD_LEASE_FAULT);no_io();
    fresh();kui_retail_hook_active=1u;assert(call()==KUI_VMU_SD_LEASE_BUSY);no_io();
    assert(kui_retail_hook_active==1u && card.device.sd.ready);
    for(unsigned variant=0;variant<6u;variant++) {
        fresh();
        if(variant==0) card.transport=KUI_STORAGE_SCIF;
        if(variant==1) card.stream.active=true;
        if(variant==2) card.device.sd.ready=false;
        if(variant==3) card.device.sd.slow=true;
        if(variant==4) card.device.sd.blocks=0;
        if(variant==5) fault=true;
        assert(call()==KUI_VMU_SD_LEASE_FAULT);no_io();
        assert(!kui_retail_hook_active && card.device.sd.ready==(variant==0u));
        if(variant==1) assert(card.stream.active);
    }
}
static void refusal_never_releases_another_owner(void) {
    const int refusals[]={KUI_LOADER_SD_NOT_READY,KUI_LOADER_SD_UNSUPPORTED};
    for(unsigned i=0;i<2u;i++) {
        fresh();acquisition_result=refusals[i];
        assert(call()==KUI_VMU_SD_LEASE_BUSY);
        assert(acquisitions==1u && !releases && !callbacks && !wire_bytes);
        assert(!kui_retail_hook_active && card.device.sd.ready);
        acquisition_result=0;assert(call()==KUI_VMU_SD_LEASE_OK);
    }
    fresh();acquisition_result=KUI_LOADER_SD_TIMEOUT;
    assert(call()==KUI_VMU_SD_LEASE_FAULT);
    assert(acquisitions==1u && !releases && !callbacks && !wire_bytes && !card.device.sd.ready);
}
static void faults_poison_after_exactly_one_release(void) {
    for(unsigned variant=0;variant<5u;variant++) {
        fresh();
        if(variant==0) callback_result=-1;
        if(variant==1) callback_result=KUI_VMU_SD_LEASE_BUSY; /* Too late for retry. */
        if(variant==2) loan_not_ready=true;
        if(variant==3) callback_fault=true;
        if(variant==4) release_fault=true;
        assert(call()==KUI_VMU_SD_LEASE_FAULT);
        assert(acquisitions==1u && releases==1u && callbacks==1u && wire_bytes==1u);
        assert(!card.device.sd.ready && !kui_retail_hook_active && !acquired);
        assert(call()==KUI_VMU_SD_LEASE_FAULT);
        assert(acquisitions==1u && releases==1u && callbacks==1u && wire_bytes==1u);
    }
}
#ifdef KUI_VMU_SD_LEASE_NATIVE_HOST_TEST
static void native_control_and_exact_pin(void) {
    const uint32_t payloads[]={UINT32_C(0x0c010020),UINT32_C(0x0c38a880),UINT32_C(0x0cfd0000)};
    for(unsigned p=0;p<sizeof(payloads)/sizeof(payloads[0]);p++) {
        fresh();
        struct kui_vmu_observe_boot_control *c=&kui_vmu_observe_boot_control;
        c->lease=payloads[p];c->worker_end=(payloads[p]|UINT32_C(0xa0000000))+UINT32_C(0x20000);
        c->terminal=(payloads[p]|TEST_CODE_ALIAS)+UINT32_C(0x200);
        native_target=(payloads[p]|TEST_CODE_ALIAS)+80u;c->sd_transaction=(uint32_t)native_target;
        assert(native_resident_transaction(c,native_target));
        assert(call()==KUI_VMU_SD_LEASE_OK);
        uint32_t first=payloads[p]-32u,end=payloads[p]+UINT32_C(0x30000);
        const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
        for(unsigned a=0;a<3u;a++) {
            assert(native_resident_destination(c,(first-4u)|aliases[a],4u));
            assert(!native_resident_destination(c,(first-4u)|aliases[a],5u));
            assert(!native_resident_destination(c,first|aliases[a],1u));
            assert(!native_resident_destination(c,(end-1u)|aliases[a],1u));
            if(end<UINT32_C(0x0d000000)) assert(native_resident_destination(c,end|aliases[a],1u));
        }
        c->sd_transaction=(uint32_t)native_target+2u;
        unsigned prior=acquisitions;assert(call()==KUI_VMU_SD_LEASE_FAULT && acquisitions==prior);
    }
    for(unsigned variant=0;variant<14u;variant++) {
        fresh();struct kui_vmu_observe_boot_control *c=&kui_vmu_observe_boot_control;
        switch(variant) {
        case 0:c->installer=UINT32_C(0x8ce00100);break;
        case 1:c->stage_stack=UINT32_C(0x8cfe0000);break;
        case 2:c->lease=0u;break;
        case 3:c->lease|=1u;break;
        case 4:c->lease|=UINT32_C(0x80000000);break;
        case 5:c->lease=UINT32_C(0x0cfd0020);break;
        case 6:c->worker_end-=UINT32_C(0x10000);break;
        case 7:c->worker_end+=UINT32_C(0x10000);break;
        case 8:c->terminal|=1u;break;
        case 9:c->terminal=UINT32_C(0x8ce00100);break;
        case 10:native_target=UINT32_C(0x0c38a980)|TEST_WRONG_CODE_ALIAS;c->sd_transaction=(uint32_t)native_target;break;
        case 11:native_target=UINT32_C(0x0c38a980);c->sd_transaction=(uint32_t)native_target;break;
        case 12:native_target=(UINT32_C(0x0c38a880)|TEST_CODE_ALIAS)+78u;c->sd_transaction=(uint32_t)native_target;break;
        default:native_target=c->worker_end;c->sd_transaction=(uint32_t)native_target;break;
        }
        assert(call()==KUI_VMU_SD_LEASE_FAULT);no_io();
    }
    for(unsigned status=0;status<16u;status++) {
        fresh();kui_vmu_observe_boot_control.status=status;
        assert((call()==KUI_VMU_SD_LEASE_OK)==(status==KUI_VMU_OBS_BOOT_INSTALLED));
        if(status!=KUI_VMU_OBS_BOOT_INSTALLED) no_io();
    }
    /* Independently compare the native callback interval/pin/control shape,
     * rather than reimplementing the function's sequence of predicates. */
    uint32_t random=UINT32_C(0x728f1251);
    for(unsigned trial=0;trial<20000u;trial++) {
        fresh();struct kui_vmu_observe_boot_control c=kui_vmu_observe_boot_control;
        random=random*UINT32_C(1664525)+UINT32_C(1013904223);
        c.lease=UINT32_C(0x0c010020)+(random%UINT32_C(0x7d7ff))*32u;
        uint32_t worker=c.lease|UINT32_C(0xa0000000);
        c.worker_end=worker+UINT32_C(0x20000);
        worker=c.lease|TEST_CODE_ALIAS;c.terminal=worker+256u;
        random=random*UINT32_C(1664525)+UINT32_C(1013904223);
        uintptr_t target=(uintptr_t)worker+(random&UINT32_C(0x3ffff));
        c.sd_transaction=(uint32_t)target;
        int expected=!(target&1u) && target>=(uintptr_t)worker+80u && target<(uintptr_t)worker+UINT32_C(0x20000);
        assert(native_resident_transaction(&c,target)==expected);
        c.sd_transaction^=2u;assert(!native_resident_transaction(&c,target));
    }
    puts("Native SCI lease: exact code alias callback, dynamic128KiB worker/192KiB lease, control mutations and20000 interval cases passed");
}
#endif
int main(void) {
#ifdef KUI_VMU_SD_LEASE_NATIVE_HOST_TEST
    native_control_and_exact_pin();
#endif
    successful_borrow_and_reentry();reject_before_io();
    refusal_never_releases_another_owner();faults_poison_after_exactly_one_release();
    puts("VMU SD lease: shared GD ownership, scoped descriptor, busy/no-I/O and persistent faults passed");
    return 0;
}
