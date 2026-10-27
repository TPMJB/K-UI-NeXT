/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the production split entry/completion over the persistent fixture.
 * No retail instructions are embedded and no production C path is stubbed. */
#define main persistent_regression_not_run
#include "test_vmu_native_persistent.c"
#undef main

/* Any complete identity other than Evolution 1's: the general build admits it
 * through the same structural/live checks, the exact build refuses it. */
static bool split_other_identity;
static void split_prepare(void) {
    static const uint8_t digest[32]={
        0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
        0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35};
    model_reset();fixture();prepare_handoff();model_card.bus.transfer_block=block_transfer;
    input.driver.image_bytes=2143652u;memcpy(input.driver.image_sha256,digest,sizeof(digest));
    if(split_other_identity) input.driver.image_sha256[31]^=1u;
    input.exits.image_bytes=input.driver.image_bytes;
    input.lease.owner.image_bytes=input.driver.image_bytes;
    input.lease.heap_bytes=0xb0000u;input.lease.lease_bytes=0x60000u;input.lease.units=0x3001u;
    put(input.lease.owner.bytes_cell,4u,input.lease.heap_bytes);
    put(input.lease.header+4u,4u,input.lease.units);
    input.runtime.lease_bytes=input.runtime.required_lease_bytes=0x60000u;
    lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
    release_saw_unpublished=false;
}
static void split_setup(void) {
    split_prepare();arm();install_hooks();
    CHECK(split_controller_geometry() && !live_plan.ready && !state.pending);
    memset(&measured_work,0,sizeof(measured_work));
}
static void split_batch(unsigned conditions,unsigned allinfo,unsigned padding) {
    stage_batch(0u,0u);
    uint32_t source=value_at(0xa05f6c04u);
    memset(memory+source-0x0c000000u,0,0x3000u);
    unsigned at=0u;
    for(unsigned i=0u;i<padding;i++) {put(source+at,4u,0x700u);at+=4u;}
    for(unsigned port=0u;port<4u;port++) {
        unsigned command=(conditions&(1u<<port))?9u:(allinfo&(1u<<port))?2u:1u;
        unsigned payload=command==9u?1u:0u;
        put(source+at,4u,payload|(port<<16)|(port==3u?0x80000000u:0u));
        put(source+at+4u,4u,rx_at(port*6u));
        put(source+at+8u,4u,(payload<<24)|(port<<22)|(port<<14)|0x2000u|command);
        if(payload) put(source+at+12u,4u,0x01000000u);
        memset(rx_bytes(port*6u),0xa5,1024u);at+=12u+payload*4u;
    }
}
static void split_done(unsigned conditions,unsigned allinfo,uint32_t parent_function) {
    physical_complete();
    for(unsigned port=0u;port<4u;port++) {
        unsigned command=(conditions&(1u<<port))?9u:(allinfo&(1u<<port))?2u:1u;
        unsigned words=command==9u?3u:command==2u?48u:28u;
        unsigned code=command==9u?8u:command==2u?6u:5u;
        put(rx_at(port*6u),4u,(words<<24)|(port<<22)|(port<<14)|0x003d0000u|code);
        put(rx_at(port*6u)+4u,4u,port?0x40000000u:parent_function);
        if(port && command==9u) put(rx_at(port*6u)+4u,4u,0x01000000u);
    }
}
static void split_submit_ok(void) {
    memset(&measured_work,0,sizeof(measured_work));
    if(setjmp(terminal)==0) kui_vmu_native_passive_split_submit(&frame);
    else {fprintf(stderr,"Split submit refused %u/%08x\n",terminal_error,state.runtime_result);CHECK(false);}
    CHECK(controller_pair.pending==NATIVE_CONTROLLER_CAPTURED && !overlay.pending);
    CHECK(state.pending && state.submitted==state.completed+1u);
    CHECK(!measured_work.heap && !measured_work.hooks && !measured_work.runtime &&
          !measured_work.layout && !measured_work.storage && !measured_work.chain);
}
static void split_success(void) {
    split_setup();
    for(unsigned round=0u;round<8u;round++) {
        unsigned conditions=round%3u==2u?1u:0u,allinfo=round%3u==1u?15u:0u;
        split_batch(conditions,allinfo,round&1u);
        uint32_t source=value_at(0xa05f6c04u);
        uint8_t tx[128];memcpy(tx,memory+source-0x0c000000u,sizeof(tx));
        split_submit_ok();
        CHECK(value_at(0xa05f6c04u)==source && !memcmp(tx,memory+source-0x0c000000u,sizeof(tx)));
        if(!round) CHECK(!live_plan.ready && !config.runtime.rx_slots[0]);
        split_done(conditions,allinfo,0x01000000u);
        uint8_t before[4][1024];
        for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
        complete_return();before[0][2]|=2u;
        for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
        CHECK(live_plan.ready && controller_pair.pending==NATIVE_CONTROLLER_IDLE);
        CHECK(!measured_work.storage && !lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
    }
}
static void split_geometry_refusals(void) {
    for(unsigned mutation=0u;mutation<11u;mutation++) {
        split_setup();
        switch(mutation) {
        case 0u:config.layout.rx_slots[0]=config.lease.payload;break;
        case 1u:config.layout.rx_slots[0]=config.tx_base;break;
        case 2u:config.layout.rx_slots[0]=config.layout.rx_slots[1];break;
        case 3u:config.layout.rx_slots[0]^=4u;break;
        case 4u:config.driver.manager_cell^=1u;break;
        case 5u:config.driver.lock_cell=0x0cfffffcu;break;
        case 6u:config.layout.rx_slots[0]=0x8c500000u;break;
        case 7u:config.layout.rx_slots[0]=physical(config.driver.manager_initial)|0xa0000000u;break;
        case 8u:config.layout.rx_slots[0]=(physical(config.driver.manager_cell)&~31u)|0xa0000000u;break;
        case 9u:config.layout.rx_slots[0]=(physical(config.driver.lock_cell)&~31u)|0xa0000000u;break;
        default:config.layout.rx_slots[0]=(physical(config.driver.completion_slot)&~31u)|0xa0000000u;break;
        }
        CHECK(!split_controller_geometry());
    }
}
static void split_deferred_refusals(void) {
    for(unsigned mutation=0u;mutation<8u;mutation++) {
        split_setup();split_batch(0u,0u,0u);
        /* These witnesses authorize our RX store, not the original physical
         * request. The tiny capture must not read them before that request. */
        if(mutation==0u) put(input.driver.submit,4u,0u);
        else if(mutation==1u) put(input.lease.owner.begin_cell,4u,0u);
        else if(mutation==2u) put(input.layout.input_array,4u,0u);
        else if(mutation==3u) put(input.layout.timer1_callback,4u,0x11111111u);
        split_submit_ok();split_done(0u,0u,mutation==4u?0x02000000u:0x01000000u);
        if(mutation==5u) put(state.source+8u,4u,value_at(state.source+8u)^1u);
        if(mutation==6u) put(0x0c300fe8u+packet_bank*24u*4u,4u,0u);
        if(mutation==7u) controller_pair.pending=3u;
        uint8_t before[4][1024];
        for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
        reject_complete();
        for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
        CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
    }
}
static void split_full_fallback(void) {
    const unsigned commands[]={1u,10u,11u,12u,13u};
    for(volatile unsigned i=0u;i<sizeof(commands)/sizeof(commands[0]);i++) {
        split_setup();stage_batch(commands[i],0u);
        if(commands[i]==12u) { /* Existing full validation owns WRITE shape. */
            put(physical(input.tx_base)+16u+16u,4u,0u);
        }
        uint8_t before_state[sizeof(state)],before_overlay[sizeof(overlay)];
        uint32_t before_fields[7];uint8_t before_frames[sizeof(controller_pair.frames)];
        memcpy(before_state,&state,sizeof(state));memcpy(before_overlay,&overlay,sizeof(overlay));
        memcpy(before_fields,&controller_pair,sizeof(before_fields));
        memcpy(before_frames,controller_pair.frames,sizeof(before_frames));
        CHECK(!split_controller_capture(&frame));
        CHECK(!memcmp(before_state,&state,sizeof(state)) && !memcmp(before_overlay,&overlay,sizeof(overlay)));
        CHECK(!memcmp(before_fields,&controller_pair,sizeof(before_fields)) &&
              !memcmp(before_frames,controller_pair.frames,sizeof(before_frames)));
        if(setjmp(terminal)==0) kui_vmu_native_passive_split_submit(&frame);
        CHECK(controller_pair.pending!=NATIVE_CONTROLLER_CAPTURED);
        CHECK(measured_work.hooks && measured_work.heap);
        CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
    }
}
static void split_warm_tag_refusals(void) {
    for(unsigned tag=1u;tag<=3u;tag+=2u) {
        split_setup();split_batch(1u,0u,0u);split_submit_ok();
        split_done(1u,0u,0x01000000u);complete_return();
        CHECK(controller_pair.count==4u && controller_pair.parent==0u);
        split_batch(1u,0u,0u);split_submit_ok();
        CHECK(!controller_pair.count && controller_pair.parent==CONTROLLER_FRAMES);
        split_done(1u,0u,0x01000000u);controller_pair.pending=tag;
        uint8_t before[4][1024];
        for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
        reject_complete();
        for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
    }
}
static void split_identity(void) {
    split_other_identity=true;
#if KUI_NATIVE_SPLIT_CONTROLLER_GENERAL
    split_success();split_geometry_refusals();split_deferred_refusals();split_full_fallback();
    split_warm_tag_refusals();
    for(unsigned mode=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE;
        mode<=KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE;mode++) {
        split_prepare();input.diagnostics=mode;
        CHECK(kui_vmu_native_passive_arm(&input,NULL)==-2 && !state.armed);
    }
#else
    split_prepare();
    CHECK(kui_vmu_native_passive_arm(&input,NULL)==-2 && !state.armed);
#endif
    split_other_identity=false;
}
#ifndef KUI_SPLIT_CONTROLLER_TEST_MAIN
#define KUI_SPLIT_CONTROLLER_TEST_MAIN main
#endif
int KUI_SPLIT_CONTROLLER_TEST_MAIN(void) {
    split_success();split_geometry_refusals();split_deferred_refusals();split_full_fallback();
    split_warm_tag_refusals();split_identity();
    printf("Split controller host checks: %u\n",checks);return 0;
}
