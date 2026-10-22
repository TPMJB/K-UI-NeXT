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
int main(void) {
    successful_borrow_and_reentry();reject_before_io();
    refusal_never_releases_another_owner();faults_poison_after_exactly_one_release();
    puts("VMU SD lease: shared GD ownership, scoped descriptor, busy/no-I/O and persistent faults passed");
    return 0;
}
