/* SPDX-License-Identifier: GPL-3.0-only */
/* The production native reader/GD core and byte-level SCI/DMA/card fixture.
 * Only CPU entry and bus ownership restoration are supplied by this test. */
#define main baseline_reader_main
#define kui_sci_sd_acquire fixture_sci_sd_acquire
#include "test_retail_async.c"
#undef kui_sci_sd_acquire
#undef main

volatile uint32_t kui_retail_hook_active,kui_retail_hook_fault;
static uint32_t actual_sr;
static bool release_fails;
static unsigned releases,stop_bytes;
static kui_retail_async_region_type fixture_region;
uint32_t kui_retail_async_vmu_test_sr(void) { return actual_sr; }
enum kui_loader_sd_result kui_sci_sd_acquire(void) {
    enum kui_loader_sd_result result=fixture_sci_sd_acquire();
    if(result==KUI_LOADER_SD_OK) R.shared.port.acquired=true;
    return result;
}
void kui_sci_sd_release(void) {
    CHECK((actual_sr&0xf0u)==0xf0u && kui_retail_hook_active==1u);
    CHECK(R.shared.port.acquired && !kui_sci_stream_busy() && !R.shared.stream.state);
    CHECK(!R.engine.release.armed && !R.engine.hooked && hw.vbr==GAME_VBR);
    ++releases;R.shared.port.acquired=false;
    if(release_fails) R.shared.port.fault=true;
}
static uint8_t loan_transfer(void *context,uint8_t value,bool slow) {
    if(R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_STOPPING) {
        CHECK((actual_sr&0xf0u)==0xf0u && kui_retail_hook_active==1u);
        CHECK(!R.engine.release.armed && !R.engine.hooked);
        ++stop_bytes;
    }
    return bus_transfer(context,value,slow);
}
static void prepare(bool opened) {
    if(!kui_retail_async_region_storage) kui_retail_async_region_storage=&fixture_region;
    actual_sr=0xf0u;kui_retail_hook_active=kui_retail_hook_fault=0u;
    release_fails=false;releases=stop_bytes=0u;
    setup(GAME_VBR,X,2000u,false);mode(2048u);
    R.shared.card.device.sd.bus.transfer=loan_transfer;
    if(opened) read_and_compare(45000u,2u,8u,0u);
    CHECK(R.engine.opened==(uint32_t)opened && R.shared.port.acquired==opened);
}
static void test_closed_loan_and_context(void) {
    prepare(false);struct kui_loader_sd copied,before;
    memset(&copied,0xa5,sizeof(copied));before=copied;
    static const uint32_t invalid[]={0u,0x10u,0xe0u,0x10000000u};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        actual_sr=invalid[i];kui_retail_async_region_type saved=R;
        unsigned clocks=card.clocks;
        CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_CONTEXT);
        CHECK(!memcmp(&saved,&R,sizeof(R)) && !memcmp(&before,&copied,sizeof(copied)));
        CHECK(card.clocks==clocks && actual_sr==invalid[i] && !kui_retail_hook_active);
    }
    for(unsigned bl=0;bl<2u;++bl) {
        actual_sr=0xf0u|(bl?0x10000000u:0u);
        CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
        CHECK(kui_retail_hook_active==1u && !releases && !stop_bytes && !hw.acquires);
        CHECK(copied.ready && copied.blocks==2048u && copied.high_capacity);
        kui_retail_async_region_type saved=R;
        unsigned clocks=card.clocks;
        kui_retail_async_call(KUI_GD_EXEC);kui_retail_async_after(KUI_GD_REQUEST,1);
        CHECK(kui_retail_async_irq()==1u);
        CHECK(!memcmp(&saved,&R,sizeof(R)) && card.clocks==clocks);
        CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_BUSY);
        uint32_t sr=actual_sr;actual_sr=0u;
        CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_CONTEXT);
        CHECK(kui_retail_hook_active==1u && R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_WRITER);
        actual_sr=sr;
        CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
        CHECK(actual_sr==sr && !kui_retail_hook_active && !R.engine.native_loan_state);
        CHECK(!R.engine.opened && !R.shared.port.acquired && !hw.acquires);
    }
}
static void test_completed_handle_and_lazy_restart(void) {
    prepare(false);
    int32_t token=request(45000u,2u,OUTPUT);
    CHECK(interrupts(100u) && R.engine.cursor.done==R.engine.cursor.count);
    CHECK(gd(KUI_GD_EXEC,0u,0u)==0 && !R.shared.service.pending);
    CHECK(R.shared.service.command==KUI_GD_DMAREAD && R.shared.service.token==(uint32_t)token);
    CHECK(!kui_sci_stream_busy() && R.engine.opened && R.shared.port.acquired);
    struct kui_retail_gd saved=R.shared.service;
    struct kui_retail_cursor cursor=R.engine.cursor;
    const struct kui_retail_manifest *map_before=R.engine.manifest;
    uint32_t destination=R.engine.destination;
    unsigned stop=card.cmd12,acquires=hw.acquires;
    struct kui_loader_sd copied={0};
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
    CHECK(card.cmd12==stop+1u && stop_bytes && releases==1u && kui_retail_hook_active==1u);
    CHECK(!R.engine.opened && !R.shared.port.acquired && !R.shared.stream.state);
    CHECK(!R.engine.release.armed && !R.engine.hooked && hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
    CHECK(!memcmp(&saved,&R.shared.service,sizeof(saved)) && !memcmp(&cursor,&R.engine.cursor,sizeof(cursor)));
    CHECK(map_before==R.engine.manifest && destination==R.engine.destination);
    /* The worker's transaction acquires/releases its own SCI bus. */
    CHECK(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);kui_sci_sd_release();
    unsigned clocks=card.clocks;
    CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
    CHECK(card.clocks==clocks && hw.acquires==acquires+1u && !R.engine.opened);
    CHECK(!memcmp(&saved,&R.shared.service,sizeof(saved)) && !memcmp(&cursor,&R.engine.cursor,sizeof(cursor)));
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_COMPLETED);
    CHECK(get(STATUS+8u)==4096u && !get(STATUS+4u));compare(45000u,2u,OUTPUT);
    read_and_compare(45002u,3u,8u,0u);
    CHECK(hw.acquires==acquires+2u && R.engine.opened && R.shared.port.acquired);
}
static void test_busy_is_immutable(void) {
    struct kui_loader_sd copied,before;
    prepare(false);memset(&copied,0xa5,sizeof(copied));before=copied;
    uint32_t *busy[]={&R.shared.service.pending,&R.shared.service.executing,
        &R.engine.active,&R.engine.in_irq,&R.engine.release.depth};
    for(unsigned i=0;i<sizeof(busy)/sizeof(busy[0]);++i) {
        *busy[i]=1u;kui_retail_async_region_type saved=R;unsigned clocks=card.clocks;
        CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_BUSY);
        CHECK(!memcmp(&saved,&R,sizeof(R)) && !memcmp(&before,&copied,sizeof(copied)));
        CHECK(card.clocks==clocks && !kui_retail_hook_active && !releases);
        *busy[i]=0u;
    }
    kui_retail_hook_active=1u;
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_BUSY);
    CHECK(kui_retail_hook_active==1u);kui_retail_hook_active=0u;
    CHECK(kui_retail_native_async_loan_begin(NULL)==KUI_VMU_NATIVE_ASYNC_LOAN_FAULT);
    CHECK(!R.engine.native_loan_state && !kui_retail_hook_fault);
    int32_t token=request(45000u,20u,OUTPUT);
    CHECK(kui_sci_stream_busy());
    kui_retail_async_region_type saved=R;unsigned clocks=card.clocks;
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_BUSY);
    CHECK(!memcmp(&saved,&R,sizeof(R)) && card.clocks==clocks && !releases);
    CHECK(finish(token,8u,0u)==KUI_GD_COMPLETED);compare(45000u,20u,OUTPUT);
}
static void quarantined(void) {
    CHECK(R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_QUARANTINED);
    CHECK(!R.shared.card.device.sd.ready && kui_retail_hook_fault && !kui_retail_hook_active);
    unsigned clocks=card.clocks;
    struct kui_loader_sd copied;
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_FAULT);
    kui_retail_async_call(KUI_GD_EXEC);kui_retail_async_after(KUI_GD_REQUEST,1);
    CHECK(kui_retail_async_irq()==1u && card.clocks==clocks);
}
static void test_begin_faults(void) {
    struct kui_loader_sd copied;
    for(unsigned fault=0;fault<8u;++fault) {
        prepare(false);
        if(fault==0u) R.shared.card.transport=KUI_STORAGE_SCIF;
        if(fault==1u) R.shared.card.device.sd.ready=false;
        if(fault==2u) R.shared.card.device.sd.slow=true;
        if(fault==3u) R.shared.card.device.sd.blocks=0u;
        if(fault==4u) R.shared.card.stream.active=true;
        if(fault==5u) R.shared.port.fault=true;
        if(fault==6u) R.shared.port.acquired=true;
        if(fault==7u) R.shared.stream.state=1u;
        unsigned clocks=card.clocks;
        CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_FAULT);
        CHECK(card.clocks==clocks && !releases);quarantined();
    }
    prepare(true);release_fails=true;
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_FAULT);
    CHECK(releases==1u);quarantined();
    prepare(true);m.healthy=false;
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_FAULT);
    CHECK(!releases);quarantined();
}
static void test_end_faults(void) {
    struct kui_loader_sd copied;
    for(unsigned fault=0;fault<16u;++fault) {
        prepare(false);
        CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
        if(fault==1u) R.shared.port.acquired=true;
        if(fault==2u) R.shared.port.fault=true;
        if(fault==3u) R.engine.opened=1u;
        if(fault==4u) R.shared.stream.state=1u;
        if(fault==5u) R.shared.card.device.sd.ready=false;
        if(fault==6u) R.engine.active=1u;
        if(fault==7u) R.engine.in_irq=1u;
        if(fault==8u) R.engine.release.depth=1u;
        if(fault==9u) R.engine.release.armed=1u;
        if(fault==10u) R.engine.hooked=1u;
        if(fault==11u) R.shared.service.pending=1u;
        if(fault==12u) R.shared.service.executing=1u;
        if(fault==13u) hw.vbr=our_vbr();
        if(fault==14u) kui_retail_hook_active=0u;
        if(fault==15u) R.shared.service.initialized=0u;
        struct kui_retail_gd saved=R.shared.service;unsigned clocks=card.clocks;
        CHECK(kui_retail_native_async_loan_end(fault!=0u)==KUI_VMU_NATIVE_ASYNC_LOAN_FAULT);
        CHECK(card.clocks==clocks && !memcmp(&saved,&R.shared.service,sizeof(saved)));
        quarantined();
    }
    prepare(false);
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
    CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
    CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_FAULT);quarantined();
}
static void test_repeated_loans(void) {
    prepare(false);
    for(unsigned i=0;i<24u;++i) {
        read_and_compare(45000u+i,1u+(i%3u),8u,0u);
        struct kui_loader_sd copied;
        CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
        CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
        CHECK(!R.engine.opened && !R.shared.port.acquired);
    }
    CHECK(hw.acquires==24u && releases==24u);
}
static void test_switched_workspace(void) {
    static kui_retail_async_region_type other_region;
    kui_retail_async_region_storage=&other_region;
    memset(&fixture_region,0x5a,sizeof(fixture_region));
    prepare(true);
    CHECK(&R==&other_region && hw.vbr==GAME_VBR);
    struct kui_loader_sd copied;
    CHECK(kui_retail_native_async_loan_begin(&copied)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
    CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
    read_and_compare(45010u,4u,8u,0u);
    const unsigned char *untouched_region=(const unsigned char *)&fixture_region;
    for(size_t i=0;i<sizeof(fixture_region);++i) CHECK(untouched_region[i]==0x5au);
    kui_retail_async_region_storage=&fixture_region;
}
int main(void) {
    kui_retail_async_region_storage=&fixture_region;
    CHECK(baseline_reader_main()==0);
    test_closed_loan_and_context();test_completed_handle_and_lazy_restart();
    test_busy_is_immutable();test_begin_faults();test_end_faults();test_repeated_loans();
    test_switched_workspace();
    printf("native async VMU loans: %u checks passed\n",checks);
    return 0;
}
