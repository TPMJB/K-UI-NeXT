/* SPDX-License-Identifier: GPL-3.0-only */
/* Retain every existing read-only/default physical-conflict regression. */
#define main readonly_overlay_baseline_main
#include "test_vmu_readonly_overlay.c"
#undef main
#include "kui/vmu_volatile.h"

static struct kui_vmu_volatile ram_card;
static uint8_t writes[32][128];
static unsigned base_calls,admit_calls;
static int base_reader(void *context,uint16_t block,uint8_t out[512]) {
    assert(context==&ram_card);++base_calls;
    return kui_vmu_image_blank_block(block,out);
}
static int volatile_admit(void *context,uint32_t h,uint32_t p0,uint32_t p1) {
    ++admit_calls;return kui_vmu_volatile_admit(context,h,p0,p1);
}
static int volatile_handler(void *context,const uint8_t *request,size_t bytes,
    uint8_t *reply,size_t capacity,size_t *reply_bytes) {
    ++calls;assert(!memcmp(rx,before,sizeof(rx)));
    if(calls==fail_call) return 0;
    return kui_vmu_volatile_handle(context,request,bytes,reply,capacity,reply_bytes);
}
static void volatile_fresh(void) {
    fresh();base_calls=admit_calls=0;
    assert(!kui_vmu_volatile_init(&ram_card,base_reader,&ram_card));
    assert(!kui_vmu_readonly_overlay_init_backend(&overlay,volatile_handler,
        volatile_admit,&ram_card));
}
static void volatile_request(unsigned index,unsigned cmd,unsigned phase,unsigned block) {
    frame(index,cmd,1u,cmd==12u?34u:cmd==1u || cmd==2u?0u:2u,
        0x02000000u,(block<<24)|(phase<<8));
    if(cmd==12u) frames[index].write_payload=writes[index];
}
static void parent_next(void) {
    frame(0,9u,32u,1u,0x01000000u,0);
    /* Deliberately retain previous virtual reply in every A1 RX slot. */
    rx[0][0]=0x03200008u;rx[0][1]=0x01000000u;
}
static void run_volatile(unsigned count) {
    parent_next();capture();assert(!prepare(count));
    assert(!memcmp(rx,before,sizeof(rx)));
    assert(!kui_vmu_readonly_overlay_complete(&overlay));
    assert(rx[0][0]==0x03210008u);
}
static void stage_block(unsigned block,uint8_t value) {
    for(unsigned phase=0;phase<4u;phase++) {
        memset(writes[1],value+phase,128u);
        volatile_request(1u,12u,phase,block);run_volatile(2u);
        assert(rx[1][0]==0x00010007u);
    }
}
static void commit_block(unsigned block) {
    volatile_request(1u,13u,4u,block);run_volatile(2u);
    assert(rx[1][0]==0x00010007u);
}
static void immutable_write_snapshot_and_stale_rx(void) {
    volatile_fresh();parent_next();
    memset(writes[1],0x31,128u);volatile_request(1u,12u,0u,42u);
    capture();assert(!prepare(2u));
    assert(!overlay.frames[1].write_payload);
    memset(writes[1],0xcc,128u);frames[1].payload1=0xffffffffu;
    assert(!kui_vmu_readonly_overlay_complete(&overlay));
    for(unsigned i=0;i<128u;i++) assert(ram_card.protocol.staged[i]==0x31u);
    assert(rx[1][0]==0x00010007u && ram_card.write_phases==1u);
    for(unsigned phase=1;phase<4u;phase++) {
        memset(writes[1],0x31u+phase,128u);
        volatile_request(1u,12u,phase,42u);run_volatile(2u);
    }
    commit_block(42u);
    assert(ram_card.cached_blocks==1u && ram_card.ram_commits==1u && !base_calls);
    volatile_request(1u,11u,0u,42u);run_volatile(2u);
    assert(rx[1][0]==0x82010008u);
    const uint8_t *data=(const uint8_t *)rx[1]+12u;
    for(unsigned i=0;i<512u;i++) assert(data[i]==0x31u+i/128u);
    assert(ram_card.cache_reads==1u && !ram_card.base_reads);
    assert(overlay.stats.reads==1u && overlay.stats.replies==6u);
}
static void filtered_physical_guards_and_snapshot_spans(void) {
    volatile_fresh();volatile_request(1u,12u,0u,42u);
    parent_next();rx[0][0]|=0x00010000u;capture();assert(!prepare(2u));
    assert(kui_vmu_readonly_overlay_complete(&overlay)==KUI_VMU_OVERLAY_CONFLICT);
    unchanged();assert(!calls && !ram_card.write_phases && !ram_card.ram_commits);
    volatile_fresh();volatile_request(1u,12u,0u,42u);
    parent_next();rx[0][0]=UINT32_MAX;capture();assert(!prepare(2u));
    assert(kui_vmu_readonly_overlay_complete(&overlay)==KUI_VMU_OVERLAY_PARENT);
    unchanged();assert(!calls);

    for(unsigned bad=0;bad<5u;bad++) {
        volatile_fresh();volatile_request(1u,12u,0u,42u);
        frames[1].write_payload=bad==0u?NULL:bad==1u?(const uint8_t *)rx[1]:
            bad==2u?(const uint8_t *)&overlay:bad==3u?(const uint8_t *)frames:
            (const uint8_t *)(uintptr_t)(UINTPTR_MAX-64u);
        prepare_refused(2u);assert(!calls);
    }
    const uint32_t invalid[]={0x0001000cu,0x2100010cu,0x2200010cu,0x0200010du};
    for(unsigned i=0;i<4u;i++) {
        volatile_fresh();volatile_request(1u,12u,0u,42u);
        frames[1].header=invalid[i];
        if(i==2u) frames[1].payload0=0x04000000u; /* LCD is never admitted. */
        if(i==3u) frames[1].payload1=(42u<<24)|(2u<<8);
        prepare_refused(2u);assert(!calls);
    }
}
static void private_ram_commit_then_later_failure_never_publishes(void) {
    volatile_fresh();stage_block(7u,0x50u);
    volatile_request(1u,13u,4u,7u);volatile_request(2u,1u,0u,0u);
    parent_next();capture();unsigned published=overlay.stats.replies;
    fail_call=calls+2u;assert(!prepare(3u));
    assert(kui_vmu_readonly_overlay_complete(&overlay)==KUI_VMU_OVERLAY_HANDLER);
    unchanged();assert(ram_card.ram_commits==1u && ram_card.cached_blocks==1u);
    assert(overlay.stats.replies==published);
    assert(kui_vmu_readonly_overlay_complete(&overlay)==KUI_VMU_OVERLAY_STATE);
    assert(!memcmp(rx,before,sizeof(rx)));
}
static void full_cache_stops_before_dma_or_phase_ack(void) {
    volatile_fresh();
    for(unsigned block=0;block<16u;block++) {stage_block(block,(uint8_t)block);commit_block(block);}
    assert(ram_card.ram_commits==16u && ram_card.cached_blocks==16u);
    stage_block(7u,0xa0u);commit_block(7u);
    assert(ram_card.ram_commits==17u && ram_card.cached_blocks==16u);
    volatile_request(1u,12u,0u,16u);parent_next();capture();
    unsigned prior_calls=calls,prior_phases=ram_card.write_phases;
    assert(prepare(2u)==KUI_VMU_OVERLAY_REQUEST);unchanged();
    assert(calls==prior_calls && ram_card.write_phases==prior_phases);
    assert(ram_card.full_faults==1u && ram_card.faulted && !base_calls);
}
int main(void) {
    assert(!readonly_overlay_baseline_main());
    immutable_write_snapshot_and_stale_rx();
    filtered_physical_guards_and_snapshot_spans();
    private_ram_commit_then_later_failure_never_publishes();
    full_cache_stops_before_dma_or_phase_ack();
    puts("VMU volatile overlay: immutable phases, filtered stale RX and atomic publication passed");
    return 0;
}
