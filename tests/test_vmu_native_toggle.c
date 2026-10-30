/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute both modes of the production General worker and genuine bound TICK. */
#define KUI_SPLIT_PREFETCH_TEST_MAIN normal_prefetch_regression_main
#include "test_vmu_native_split_prefetch.c"
#include "kui/vmu_native_toggle.h"

static void off_setup(void) {
    split_prepare();
    input.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_READER_ONLY;
    memset(&input.durable,0,sizeof(input.durable));
    arm();install_hooks();reader_bind();memset(&reader_stream,0,sizeof(reader_stream));
    CHECK(reader_only() && !durable_card.ready && !durable_card.faulted);
    CHECK(config.lease.lease_bytes==UINT32_C(0x51000) && KUI_NATIVE_RA_DATA_BYTES==UINT32_C(0x30000));
}
static void no_virtual_io(void) {
    CHECK(!overlay.stats.advertisements && !overlay.stats.backend_calls && !overlay.stats.replies);
    CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
    CHECK(!durable_card.ready && !durable_card.reads && !durable_card.commits && !durable_card.commit_attempts);
    idle();
}
static void off_poll(unsigned attached) {
    split_batch(1u,0u,0u);split_submit_ok();split_done(1u,0u,UINT32_C(0x01000000));
    put(rx_at(0u),4u,value_at(rx_at(0u))|((attached&31u)<<16));
    uint8_t before[4][1024];
    for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
    complete_return();
    for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
    no_virtual_io();
}
static void off_accessory(unsigned address,unsigned command,bool present,unsigned error,
    uint32_t function) {
    stage_batch(command,42u);
    unsigned port=address>>6,unit=address&63u,slot=port*6u+(unit==1u?1u:2u);
    uint32_t source=value_at(UINT32_C(0xa05f6c04));
    uint32_t header=value_at(source+24u);
    put(source+16u,4u,(value_at(source+16u)&~UINT32_C(0x30000))|(port<<16));
    put(source+20u,4u,rx_at(slot));
    put(source+24u,4u,(header&UINT32_C(0xff0000ff))|(address<<8)|((address&192u)<<16));
    if((header>>24)!=0u) put(source+28u,4u,function);
    memset(rx_bytes(slot),0xa5,1024u);
    if(command==12u) memset(memory+source+36u-UINT32_C(0x0c000000),0x62,128u);
    uint8_t original[0x3000];memcpy(original,memory+source-UINT32_C(0x0c000000),sizeof(original));
    submit();CHECK(!overlay.pending && !controller_pair.pending);
    CHECK(value_at(UINT32_C(0xa05f6c04))==source);
    CHECK(!memcmp(original,memory+source-UINT32_C(0x0c000000),sizeof(original)));
    put(UINT32_C(0xa05f6c18),4u,1u);physical_complete();
    if(present && port<2u) put(rx_at(port*6u),4u,value_at(rx_at(port*6u))|(unit<<16));
    if(!present) put(rx_at(slot),4u,UINT32_MAX);
    else if(error) put(rx_at(slot),4u,(address<<16)|((address&192u)<<8)|error);
    else {
        unsigned words=command==1u?28u:command==2u?48u:command==10u?7u:command==11u?
            function==UINT32_C(0x08000000)?3u:function==UINT32_C(0x04000000)?49u:130u:0u;
        unsigned code=command==1u?5u:command==2u?6u:command==10u || command==11u?8u:7u;
        put(rx_at(slot),4u,(words<<24)|(address<<16)|((address&192u)<<8)|code);
        if(words) put(rx_at(slot)+4u,4u,function);
        if(command==11u) put(rx_at(slot)+8u,4u,42u<<24);
    }
    uint8_t before[3][1024];
    memcpy(before[0],rx_bytes(0u),1024u);memcpy(before[1],rx_bytes(slot),1024u);
    memcpy(before[2],rx_bytes(6u),1024u);
    const unsigned ticks=tick_visits;complete_return();
    CHECK(tick_visits==ticks); /* The unchanged full-path schedule has no TICK. */
    CHECK(!memcmp(before[0],rx_bytes(0u),1024u) && !memcmp(before[1],rx_bytes(slot),1024u));
    CHECK(!memcmp(before[2],rx_bytes(6u),1024u));no_virtual_io();
}
static void off_passthrough_suite(void) {
    off_setup();
    unsigned ticks=tick_visits;
    for(unsigned i=0u;i<16u;i++) off_poll(i&1u?3u:0u);
    CHECK(tick_visits==ticks+16u);
    static const unsigned commands[]={1u,2u,3u,10u,11u,12u,13u};
    for(unsigned port=0u;port<4u;port++) for(unsigned unit=1u;unit<=2u;unit++) {
        unsigned address=(port<<6)|unit;
        for(unsigned i=0u;i<sizeof(commands)/sizeof(commands[0]);i++) {
            off_accessory(address,commands[i],false,0u,UINT32_C(0x02000000));
            off_accessory(address,commands[i],true,0u,UINT32_C(0x02000000));
            off_accessory(address,commands[i],true,0xfcu,UINT32_C(0x02000000));
        }
        off_accessory(address,11u,true,0u,UINT32_C(0x08000000)); /* Short clock reply. */
        off_accessory(address,11u,true,0u,UINT32_C(0x04000000)); /* LCD function. */
        off_accessory(address,12u,true,0u,UINT32_C(0x04000000)); /* LCD write. */
        off_accessory(address,12u,true,0u,UINT32_C(0x08000000)); /* Clock write. */
        off_accessory(address,11u,true,UINT32_C(0x07df0011),UINT32_C(0x08000000)); /* Opaque physical error. */
    }
    uint8_t request[12]={0},response[16]={0};size_t bytes=0u;
    CHECK(!persistent_handler(&durable_card,request,sizeof(request),response,sizeof(response),&bytes));
    CHECK(!persistent_admit(&durable_card,UINT32_C(0x0200020b),UINT32_C(0x02000000),0u));
    CHECK(persistent_lease(kui_vmu_native_persistent_sd_transaction,&durable_card)==KUI_VMU_SD_LEASE_FAULT);
    struct kui_loader_sd loan={.ready=true};
    CHECK(kui_vmu_native_persistent_sd_transaction(&durable_card,&loan)==-1 && !loan.ready);
    no_virtual_io();
}
static void off_stream_guard_suite(void) {
    off_setup();
    CHECK(guard_run(1u,0u)==1u);
    CHECK(guard_run(64u,2u)==64u);
    CHECK(guard_run(64u,8u)==5u);
    CHECK(guard_run(32u,0u)==13u);
    CHECK(guard_run(32u,0u)==32u && reader_stream.sum==0u);
    no_virtual_io();
}
static void off_opaque_packets(void) {
    off_setup();
    for(unsigned words=0u;words<256u;words++) {
        stage_batch(0u,0u);
        uint32_t source=value_at(UINT32_C(0xa05f6c04));
        put(source+16u,4u,words);put(source+20u,4u,rx_at(2u));
        put(source+24u,4u,(words<<24)|UINT32_C(0x200)|((words%253u)+1u));
        for(unsigned i=0u;i<words;i++) put(source+28u+i*4u,4u,
            i?UINT32_C(0x42756100)+i:UINT32_C(0x08000000));
        unsigned next=28u+words*4u;
        put(source+next,4u,UINT32_C(0x80010000));put(source+next+4u,4u,rx_at(6u));
        put(source+next+8u,4u,UINT32_C(0x00406001));
        uint8_t tx[0x3000];memcpy(tx,memory+source-UINT32_C(0x0c000000),sizeof(tx));
        submit();CHECK(value_at(UINT32_C(0xa05f6c04))==source);
        put(UINT32_C(0xa05f6c18),4u,1u);physical_complete();
        for(unsigned i=0u;i<1024u;i++) rx_bytes(2u)[i]=(uint8_t)(i+words);
        uint8_t rx[3][1024];memcpy(rx[0],rx_bytes(0u),1024u);
        memcpy(rx[1],rx_bytes(2u),1024u);memcpy(rx[2],rx_bytes(6u),1024u);
        unsigned ticks=tick_visits;complete_return();CHECK(tick_visits==ticks);
        CHECK(!memcmp(tx,memory+source-UINT32_C(0x0c000000),sizeof(tx)));
        CHECK(!memcmp(rx[0],rx_bytes(0u),1024u) && !memcmp(rx[1],rx_bytes(2u),1024u));
        CHECK(!memcmp(rx[2],rx_bytes(6u),1024u));no_virtual_io();
    }
}
static void off_refusal_suite(void) {
    split_prepare();input.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_READER_ONLY;
    CHECK(kui_vmu_native_passive_arm(&input,NULL)==-3 && !state.armed); /* No dormant authority. */
    for(unsigned mutation=0u;mutation<3u;mutation++) {
        off_setup();stage_batch(11u,42u);put(rx_at(2u),4u,UINT32_MAX);submit();physical_complete();
        put(rx_at(2u),4u,UINT32_MAX);
        if(!mutation) put(value_at(UINT32_C(0xa05f6c04))+24u,4u,0u);
        else if(mutation==1u) put(config.driver.completion_slot,4u,0u);
        else bad_guard=1u;
        uint8_t before[3][1024];
        memcpy(before[0],rx_bytes(0u),1024u);memcpy(before[1],rx_bytes(2u),1024u);memcpy(before[2],rx_bytes(6u),1024u);
        const unsigned ticks=tick_visits;reject_complete();CHECK(tick_visits==ticks);
        CHECK(!memcmp(before[0],rx_bytes(0u),1024u) && !memcmp(before[1],rx_bytes(2u),1024u));
        CHECK(!memcmp(before[2],rx_bytes(6u),1024u));no_virtual_io();
    }
}
static void capability_suite(void) {
    uint32_t words[16]={KUI_VMU_NATIVE_TOGGLE_MAGIC0,KUI_VMU_NATIVE_TOGGLE_MAGIC1,1u,32u,7u,
        UINT32_C(0x51000),UINT32_C(0x30000),1u};
    size_t offset=SIZE_MAX;
    CHECK(kui_vmu_native_toggle_find(words,32u,&offset)==1 && offset==0u);
    memcpy(words+8u,words,32u);CHECK(kui_vmu_native_toggle_find(words,sizeof(words),&offset)==-1);
    CHECK(kui_vmu_native_toggle_find(words,31u,&offset)==0);
    for(unsigned i=0u;i<8u;i++) {
        words[i]^=1u;CHECK(kui_vmu_native_toggle_find(words,32u,&offset)==0);words[i]^=1u;
    }
    /* The GUI accepts exactly the two reviewed complete layouts. Mixed
     * General/compact descriptors are still ambiguous, never first-match. */
    words[5]=UINT32_C(0x47000);offset=SIZE_MAX;
    CHECK(kui_vmu_native_toggle_find(words,32u,&offset)==1 && offset==0u);
    CHECK(kui_vmu_native_toggle_find(words,sizeof(words),&offset)==-1);
    for(unsigned i=0u;i<8u;i++) {
        words[i]^=1u;CHECK(kui_vmu_native_toggle_find(words,32u,&offset)==0);words[i]^=1u;
    }
    static const uint32_t refused[]={0u,0x12000u,0x46000u,0x48000u,0x51001u,0x55000u,UINT32_MAX};
    for(unsigned i=0u;i<sizeof(refused)/sizeof(refused[0]);i++) {
        words[5]=refused[i];offset=SIZE_MAX;
        CHECK(kui_vmu_native_toggle_find(words,32u,&offset)==0 && offset==SIZE_MAX);
    }
}
int main(void) {
    CHECK(normal_prefetch_regression_main()==0);
    off_passthrough_suite();off_stream_guard_suite();off_opaque_packets();off_refusal_suite();capability_suite();
    printf("VMU toggle General worker: %u checks; normal behavior, physical VMUs on all ports and opaque packets, full prefetch and stream guard, zero virtual-card SD I/O\n",checks);
    return 0;
}
