/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual GD/SCI cursor and card model. The separately owned auxiliary visit
 * models only its cache reset/quiesce handshake, whose own policy suite tests
 * the complete read-ahead implementation. No guest copy is replaced here. */
#define KUI_NATIVE_SDK_SCI_MAIN sdk_baseline_main
#define kui_sci_sd_acquire fixture_sci_sd_acquire
#include "test_retail_async_sdk_sci_route.c"
#undef kui_sci_sd_acquire
#include "kui/native_readahead.h"
#include "kui/native_disc_set.h"
#include "kui/native_session_binding.h"

static const struct kui_native_session_binding native_session_binding={
    KUI_NATIVE_SESSION_BINDING_MAGIC,KUI_NATIVE_SESSION_BINDING_VERSION,
    sizeof(struct kui_native_session_binding),UINT32_C(0x8c007500),
    UINT32_C(0x8c007504),UINT32_C(0x8c007508)
};
#include KUI_SESSION_BINDING_TEST_HEADER

static uint32_t actual_sr;
static unsigned resets,quiesces,releases;
static bool reset_refused,old_cached;
uint32_t kui_retail_async_vmu_test_sr(void) {return actual_sr;}
enum kui_loader_sd_result kui_sci_sd_acquire(void) {
    enum kui_loader_sd_result result=fixture_sci_sd_acquire();
    if(result==KUI_LOADER_SD_OK)R.shared.port.acquired=true;
    return result;
}
void kui_sci_sd_release(void) {
    CHECK((actual_sr&0xf0u)==0xf0u && kui_retail_hook_active==1u && R.shared.port.acquired);
    CHECK(!kui_sci_stream_busy() && !R.shared.stream.state && !R.engine.hooked && !R.engine.release.armed);
    ++releases;R.shared.port.acquired=false;
}
static uint32_t auxiliary(uint32_t event,uint32_t arg,kui_retail_async_region_type *region,uint32_t reserved) {
    CHECK(region==&R && !reserved);
    if(event==KUI_NATIVE_RA_RESET || event==KUI_NATIVE_RA_QUIESCE) {
        CHECK(!arg);
        CHECK(R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_IDLE);
        if(event==KUI_NATIVE_RA_RESET) {
            CHECK(!kui_retail_hook_active);++resets;
            if(reset_refused)return 0;
            old_cached=false;
        } else {CHECK(kui_retail_hook_active==1u && !old_cached);++quiesces;}
        return 1u;
    }
    return 0u; /* Real native reader supplies ordinary foreground data. */
}
static void prepare(void) {
    actual_sr=0xf0u;resets=quiesces=releases=0u;reset_refused=false;old_cached=false;
    setup_sdk(X,true);
    R.vector600[KUI_NATIVE_RA_ENTRY_INDEX]=(uint32_t)(uintptr_t)auxiliary;
}
static void begin_guards(void) {
    prepare();struct kui_loader_sd loan={0};
    uint32_t *busy[]={&R.shared.service.pending,&R.shared.service.executing,&R.engine.active,
        &R.engine.in_irq,&R.engine.release.depth};
    for(unsigned i=0;i<sizeof(busy)/sizeof(busy[0]);i++) {
        *busy[i]=1u;kui_retail_async_region_type saved=R;unsigned clocks=card.clocks;
        CHECK(kui_retail_async_session_begin(&loan)==0 && !memcmp(&saved,&R,sizeof(R)));
        CHECK(!resets && !quiesces && card.clocks==clocks);*busy[i]=0u;
    }
    actual_sr=0u;CHECK(kui_retail_async_session_begin(&loan)==-1 && !resets);actual_sr=0xf0u;
    CHECK(kui_retail_async_session_begin(NULL)==-1 && !resets);
    old_cached=true;reset_refused=true;
    CHECK(kui_retail_async_session_begin(&loan)==0 && resets==1u && old_cached && !quiesces);
    reset_refused=false;
    CHECK(kui_retail_async_session_begin(&loan)==1 && !old_cached && resets==2u && quiesces==1u);
    CHECK(R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_WRITER && kui_retail_hook_active==1u);
    CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
}
static void map_change(void) {
    prepare();read_and_compare(45000u,2u,0u,1u);
    struct kui_retail_manifest next=manifest;
    next.storage_transport=KUI_STORAGE_SCI;
    unsigned index=kui_retail_track_first_extent(&next.slots[2].track);
    struct kui_retail_extent *extent=&next.slots[index].extent;
    CHECK(extent->blocks<=48u);extent->card_lba=1900u;
    for(uint32_t i=0;i<extent->blocks*512u;i++)image_card[1900u*512u+i]=(uint8_t)(i*7u+0x5bu);
    CHECK(kui_retail_manifest_validate(&next)==KUI_GAME_OK);
    struct kui_loader_sd loan;old_cached=true;
    CHECK(kui_retail_async_session_begin(&loan)==1 && resets==1u && !old_cached && releases==1u);
    uint32_t sdk=R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX];
    CHECK(kui_retail_gd_disc_open(&R.shared.service)==0);
    CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
    CHECK(gd(KUI_GD_DRIVE,STATUS,0)==0 && get(STATUS)==6u);
    CHECK(kui_retail_gd_disc_absent(&R.shared.service)==0);
    CHECK(kui_retail_async_session_begin(&loan)==1);
    CHECK(kui_retail_gd_disc_close(&R.shared.service,&next)==0);
    kui_retail_async_session_rebind(&next);
    CHECK(R.engine.manifest==&next && !R.engine.cursor.manifest && !R.engine.token && !R.engine.destination);
    CHECK(!R.engine.failed && !R.engine.since && !R.engine.retries && !R.engine.active && !R.engine.opened);
    CHECK(R.vector600[KUI_NATIVE_SDK_SCI_VBR_INDEX]==sdk);
    CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
    CHECK(kui_retail_image_init(&reference,&next,read_block,NULL)==KUI_GAME_OK);
    read_and_compare(45000u,2u,0u,1u);
    CHECK(ram[OUTPUT-BEGIN]==0x5bu && R.engine.disc_generation==R.shared.service.disc_generation);
}
static void stale_destination(void) {
    prepare();int32_t token=request(45000u,20u,OUTPUT);CHECK(token>0 && R.engine.cursor.write);
    uint8_t before[512],late[512];memcpy(before,ram+OUTPUT-BEGIN,sizeof(before));memset(late,0x31,sizeof(late));
    unsigned writes=hw.writes;--R.engine.disc_generation;
    R.engine.cursor.write(R.engine.cursor.context,0,late,sizeof(late));
    CHECK(hw.writes==writes && !memcmp(before,ram+OUTPUT-BEGIN,sizeof(before)));
    CHECK(R.engine.failed==KUI_GD_ERROR_UNAVAILABLE && !R.engine.active);
    kui_retail_gd_progress_epoch(&R.shared.service,R.engine.disc_generation,(uint32_t)token,20,0);
    CHECK(R.shared.service.pending && !R.shared.service.completed_bytes);
}
static void faults(void) {
    prepare();R.shared.card.device.sd.ready=false;struct kui_loader_sd loan;
    CHECK(kui_retail_async_session_begin(&loan)==-1 && !resets && kui_retail_hook_fault);
    CHECK(R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_QUARANTINED);
    prepare();kui_retail_async_session_rebind(&manifest);
    CHECK(kui_retail_hook_fault && R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_QUARANTINED);
}
static void binding_getter(void) {
    prepare();struct kui_loader_sd loan;memset(&loan,0x91,sizeof(loan));
    struct kui_loader_sd before=loan;kui_retail_async_region_type saved=R;
    unsigned clocks=card.clocks,writes=hw.writes;
    const uint32_t rejected[]={KUI_NATIVE_SESSION_STATUS,KUI_NATIVE_SESSION_DISC_OPEN,
        KUI_NATIVE_SESSION_DISC_CLOSE,KUI_NATIVE_SESSION_RETURN,5u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(rejected)/sizeof(rejected[0]);i++)
        CHECK(kui_retail_native_session_control(rejected[i],&manifest,&loan)==-1);
    CHECK((uint32_t)kui_retail_native_session_control(KUI_NATIVE_SESSION_BIND,&manifest,&loan)==
        (uint32_t)(uintptr_t)&native_session_binding);
    CHECK(native_session_binding.magic==KUI_NATIVE_SESSION_BINDING_MAGIC &&
        native_session_binding.version==1u && native_session_binding.bytes==24u);
    CHECK(!memcmp(&saved,&R,sizeof(R)) && !memcmp(&before,&loan,sizeof(loan)));
    CHECK(!resets && !quiesces && !releases && card.clocks==clocks && hw.writes==writes);
}
int main(void) {
    begin_guards();map_change();stale_destination();faults();binding_getter();
    printf("PASS native async session: %u checks; reset-before-loan, idle/health guards, real SCI rebind, stale destination fence and read-only binding getter\n",checks);
    return 0;
}
