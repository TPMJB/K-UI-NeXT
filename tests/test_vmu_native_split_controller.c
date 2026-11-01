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
    /* The independent VMU build owns exactly its worker, with no reader
     * map, stream state, auxiliary or cache following it. */
#if KUI_NATIVE_VMU_ONLY
    const uint32_t lease_bytes=KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES;
#else
    const uint32_t lease_bytes=KUI_NATIVE_RA_LEASE_BYTES;
#endif
    input.lease.heap_bytes=0x50000u+lease_bytes;input.lease.lease_bytes=lease_bytes;input.lease.units=lease_bytes/32u+1u;
    put(input.lease.owner.bytes_cell,4u,input.lease.heap_bytes);
    put(input.lease.header+4u,4u,input.lease.units);
    input.runtime.lease_bytes=input.runtime.required_lease_bytes=lease_bytes;
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
        /* Every main unit reports sub-units in slots 3-5 and the physical
         * card's controller A slot, never the virtual card's. */
        put(rx_at(port*6u),4u,(words<<24)|(port<<22)|(port<<14)|((0x3cu|PHYSICAL)<<16)|code);
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
#if KUI_NATIVE_SPLIT_CONTROLLER_GENERAL
static void split_full_submit_ok(void) {
    if(setjmp(terminal)==0) kui_vmu_native_passive_split_submit(&frame);
    else {fprintf(stderr,"Split fallback refused %u/%08x\n",terminal_error,state.runtime_result);CHECK(false);}
    CHECK(!controller_pair.pending && overlay.pending);
}
#endif
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
        complete_return();before[0][2]|=VIRTUAL;
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
#if KUI_NATIVE_SPLIT_CONTROLLER_GENERAL
/* The early server can be called at any inherited IMASK and keeps its own
 * U0/P1 lock pointer. Exercise the same deferred publication proof, on both
 * an allocator lease and HOTD2's no-allocator arena, without any SD loan for
 * controller-only transfers. */
static void split_early_setup(unsigned cached,unsigned arena) {
    split_prepare();input.driver.family=KUI_VMU_NATIVE_FAMILY_EARLY_V1;
    if(cached) input.driver.lock_cell|=UINT32_C(0x80000000);
    put(input.driver.lock_cell,4u,kui_vmu_native_lock_pointer(&input.driver));
    if(arena) {
        const struct kui_vmu_native_heap owner={.version=KUI_VMU_NATIVE_HEAP_VERSION,
            .bytes=sizeof(owner),.family=KUI_VMU_NATIVE_HEAP_ARENA,
            .image_bytes=input.driver.image_bytes,.load=input.driver.load_address,
            .initializer_slot=physical(input.lease.owner.initializer_slot)|UINT32_C(0x80000000)};
        CHECK(kui_vmu_native_heap_arena_lease(&owner,input.lease.payload,input.lease.lease_bytes,
            input.lease.lease_bytes,&input.lease));
        kui_vmu_native_readonly_release_resume=input.lease.owner.release+20u;
        for(unsigned i=0u;i<8u;i++) put(input.lease.payload+4u*i,4u,0x4b554900u+i);
    }
    input.exits.family=KUI_VMU_NATIVE_EXIT_EARLY_PAIR;input.exits.count=2u;
    input.exits.entries[1]=input.exits.entries[0]+8u;
    input.exits.entries[2]=input.exits.exec_thunk=0u;
    arm();install_hooks();
    uint16_t patch[4];CHECK(kui_vmu_native_patch_encode_head(input.exits.entries[0],patch));
    for(unsigned byte=0u;byte<8u;byte++)
        put(input.exits.entries[0]+byte,1u,(patch[byte/2u]>>(8u*(byte&1u)))&255u);
    CHECK(installed_hooks_valid() && split_controller_geometry() && !live_plan.ready);
    held_report=0u;
}
static void split_early_sessions(void) {
    unsigned sessions=0u;
    for(unsigned cached=0u;cached<2u;cached++) for(unsigned arena=0u;arena<2u;arena++)
        for(unsigned level=0u;level<16u;level++) {
            split_early_setup(cached,arena);
            submit_sr=0x60000101u|(level<<4);
            complete_sr=0x60000101u|(((level*7u)&15u)<<4);
            for(unsigned round=0u;round<4u;round++) {
                unsigned conditions=round==1u?0u:1u,allinfo=round==1u?15u:0u;
                split_batch(conditions,allinfo,round&1u);
                uint32_t source=value_at(0xa05f6c04u);
                uint8_t tx[128];memcpy(tx,memory+source-0x0c000000u,sizeof(tx));
                split_submit_ok();
                CHECK(value_at(0xa05f6c04u)==source && !memcmp(tx,memory+source-0x0c000000u,sizeof(tx)));
                split_done(conditions,allinfo,0x01000000u);
                uint8_t before[4][1024];
                for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
                complete_return();before[0][2]|=VIRTUAL;
                for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
                CHECK(live_plan.ready && !state.pending && !controller_pair.pending);
                CHECK(!measured_work.storage && !lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
            }
            /* Virtual requests still take the complete overlay/storage route. */
            stage_batch(11u,42u);CHECK(!split_controller_capture(&frame));
            split_full_submit_ok();
            physical_complete();complete_return();
            CHECK(durable_card.reads==1u && lease_calls==1u && sd.bytes && !sd.writes);
            ++sessions;
        }
    printf("Early split controller: %u sessions across both lock aliases, both lease kinds and all inherited levels\n",sessions);
}
static void split_early_refusals(void) {
    /* A fast-path miss cannot weaken the ordinary family's IRQ contract. */
    for(unsigned level=0u;level<16u;level++) {
        if(level==6u || level==15u) continue;
        split_setup();submit_sr=0x60000101u|(level<<4);split_batch(1u,0u,0u);
        CHECK(!split_controller_capture(&frame) && !state.pending && !controller_pair.pending);
        CHECK(!lease_calls && !sd.bytes && !sd.writes);
    }
    for(unsigned cached=0u;cached<2u;cached++) for(unsigned mutation=0u;mutation<5u;mutation++) {
        split_early_setup(cached,1u);submit_sr=complete_sr=0x60000101u;
        split_batch(1u,0u,0u);
        if(mutation<2u) {
            if(!mutation) put(kui_vmu_native_lock_pointer(&input.driver),1u,0u);
            else put(input.driver.lock_cell,4u,
                (physical(input.driver.lock_cell)+4u)|UINT32_C(0xa0000000));
            CHECK(!split_controller_capture(&frame) && !state.pending && !controller_pair.pending);
            continue;
        }
        split_submit_ok();split_done(1u,0u,0x01000000u);
        if(mutation==2u) put(kui_vmu_native_lock_pointer(&input.driver),1u,0u);
        else if(mutation==3u) put(input.driver.submit,4u,0u);
        else put(input.lease.payload,4u,0u);
        uint8_t before[4][1024];
        for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
        reject_complete();
        for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
        CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
    }
}
static void split_early_work(void) {
    unsigned reads[2][2]={{0u}};
    for(unsigned path=0u;path<2u;path++) {
        split_early_setup(1u,1u);submit_sr=complete_sr=0x60000101u;
        split_batch(1u,0u,0u);split_submit_ok();split_done(1u,0u,0x01000000u);complete_return();
        split_batch(1u,0u,0u);
        unsigned before=bus_reads;
        if(path) split_submit_ok();else submit();
        reads[path][0]=bus_reads-before;
        split_done(1u,0u,0x01000000u);complete_return();
        reads[path][1]=measured_complete_reads;
        CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
    }
    /* Deferral must not add extra guest-bus work to this warm completion;
     * the split path saves pre-DMA work without repeating its TX witness. */
    CHECK(reads[1][1]<=reads[0][1]);
    CHECK(reads[1][0]+reads[1][1]<reads[0][0]+reads[0][1]);
    printf("Early controller RAM/MMIO reads (not cycles): full %u+%u, split %u+%u\n",
        reads[0][0],reads[0][1],reads[1][0],reads[1][1]);
}
static unsigned split_observed_rx_reads;
static void split_observe_rx_read(uint32_t address,unsigned width) {
    for(unsigned slot=0u;slot<KUI_VMU_NATIVE_RX_SLOTS;slot++) {
        uint32_t rx=config.layout.rx_slots[slot];
        if(rx && physical(address)>=physical(rx) &&
           physical(address)+width<=physical(rx)+1024u) ++split_observed_rx_reads;
    }
}
static void split_early_source_refusals(void) {
    /* Every real TX word must still be checked before any captured RX is
     * followed, including on a warm plan after removing the redundant scan. */
    for(unsigned warm=0u;warm<2u;warm++) for(unsigned word=0u;word<16u;word++) {
        split_early_setup(1u,1u);submit_sr=complete_sr=0x60000101u;
        if(warm) {split_batch(15u,0u,0u);split_submit_ok();
            split_done(15u,0u,0x01000000u);complete_return();}
        split_batch(15u,0u,0u);split_submit_ok();
        CHECK(controller_pair.words==16u);
        split_done(15u,0u,0x01000000u);
        uint32_t address=state.source+4u*word;
        put(address,4u,value_at(address)^1u);
        uint8_t before[4][1024];
        for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
        split_observed_rx_reads=0u;read_observer=split_observe_rx_read;
        reject_complete();read_observer=NULL;
        CHECK(!split_observed_rx_reads && terminal_error==0x47u);
        for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
        CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
    }
}
static void split_early_held_round(uint32_t controls,bool full,bool report) {
    if(full) begin_batch(1u,0u,0u,NULL);
    else {split_batch(1u,0u,0u);split_submit_ok();}
    if(full) physical_complete();else split_done(1u,0u,0x01000000u);
    put(rx_at(0)+8u,4u,controls);
    if(!report) {complete_return();return;}
    uint8_t before[4][1024];
    for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
    reject_complete();CHECK(terminal_error==0x4fu);
    for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
    CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
}
static void split_early_report(void) {
    const uint32_t held=UINT32_C(0xfffff9f9);
    const uint32_t near[]={UINT32_C(0xc7fff9f9),UINT32_C(0xffc7f9f9),UINT32_C(0xfffff9fd),
        UINT32_C(0xfffff9fb),UINT32_C(0xfffffbf9),UINT32_C(0xfffffdf9),UINT32_MAX};
    split_early_setup(1u,1u);submit_sr=complete_sr=0x60000101u;
    for(unsigned i=0u;i<sizeof(near)/sizeof(*near);i++) {
        for(unsigned round=0u;round<179u;round++) split_early_held_round(held,false,false);
        split_early_held_round(near[i],false,false);CHECK(!held_report);
    }
    /* Port A INFO carries no controls and resets the held count. */
    for(unsigned round=0u;round<179u;round++) split_early_held_round(held,false,false);
    split_batch(0u,0u,0u);split_submit_ok();split_done(0u,0u,0x01000000u);complete_return();
    CHECK(!held_report);
    /* Alternating full and split completions preserve one continuous hold. */
    for(unsigned round=0u;round<179u;round++) split_early_held_round(held,round&1u,false);
    CHECK(held_report==179u);split_early_held_round(held,false,true);
    /* Every read on the cold early completion fails closed, including the
     * diagnostic control word, before any parent-byte or virtual/SD write. */
    split_early_setup(1u,1u);submit_sr=complete_sr=0x60000101u;
    split_batch(1u,0u,0u);split_submit_ok();split_done(1u,0u,0x01000000u);complete_return();
    unsigned count=measured_complete_reads;
    for(unsigned which=1u;which<=count;which++) {
        split_early_setup(1u,1u);submit_sr=complete_sr=0x60000101u;
        split_batch(1u,0u,0u);split_submit_ok();split_done(1u,0u,0x01000000u);
        uint8_t before[4][1024];
        for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
        fail_read=bus_reads+which;reject_complete();
        for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
        CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
    }
    printf("Early split report: 180-frame hold across mixed paths; %u completion read faults refused before publication\n",count);
}
static void split_long_sessions(void) {
    const uint32_t starts[]={PAIR_LIMIT-1u,PAIR_LIMIT,PAIR_LIMIT+1u,
        UINT32_MAX-1u,UINT32_MAX,0u};
    for(unsigned route=0u;route<2u;route++)
        for(unsigned i=0u;i<sizeof(starts)/sizeof(*starts);i++) {
            split_setup();state.submitted=state.completed=starts[i];
            for(uint32_t n=0u;n<5u;n++) {
                if(!route) {
                    split_batch(1u,0u,0u);split_submit_ok();
                    split_done(1u,0u,0x01000000u);complete_return();
                } else transfer_batch(1u,0u,0u,NULL);
                CHECK(state.submitted==starts[i]+n+1u && state.completed==state.submitted);
                CHECK(!state.pending && !state.error && !durable_card.faulted);
            }
        }
    /* Four WRITE phases cross the uint32 boundary. Final SYNC and READ then
     * exercise the complete storage-ownership and durable journal path. */
    uint8_t next[512];for(unsigned i=0u;i<sizeof(next);i++) next[i]=(uint8_t)(i^0x9bu);
    split_setup();state.submitted=state.completed=UINT32_MAX-2u;
    stage_block(42u,next);transfer_batch(13u,42u,4u,NULL);
    CHECK(durable_card.commits==1u && value_at(rx_at(VIRTUAL))==((VIRTUAL<<16)|7u));
    read_compare(42u,next);CHECK(state.submitted==3u && state.completed==3u);
    CHECK(release_saw_unpublished);protect_media();
    model_reopen();
    struct kui_vmu_backing backing={0};struct kui_vmu_store reopened={0};
    CHECK(!kui_vmu_backing_init(&backing,&model_bus,&backing_config));
    struct kui_vmu_store_io io=kui_vmu_backing_io(&backing);
    CHECK(!kui_vmu_store_open(&reopened,&io,&store_config));
    CHECK(!kui_vmu_store_checkpoint(&reopened));
    CHECK(!memcmp(sd.media[image_lba(42u)],next,512u));
    CHECK(!memcmp(sd.media[950u],(uint8_t[512]){0},512u));protect_media();
    /* A zero submission count after wrap grants no authority to publish. */
    split_setup();state.submitted=state.completed=UINT32_MAX;
    split_batch(1u,0u,0u);split_submit_ok();CHECK(!state.submitted && state.pending);
    split_done(1u,0u,0x01000000u);
    uint8_t before[4][1024];
    for(unsigned port=0u;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
    put(input.driver.submit,4u,0u);reject_complete();
    for(unsigned port=0u;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
    CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
}
#endif
#ifndef KUI_SPLIT_CONTROLLER_TEST_MAIN
#define KUI_SPLIT_CONTROLLER_TEST_MAIN main
#endif
int KUI_SPLIT_CONTROLLER_TEST_MAIN(void) {
    split_success();split_geometry_refusals();split_deferred_refusals();split_full_fallback();
    split_warm_tag_refusals();split_identity();
#if KUI_NATIVE_SPLIT_CONTROLLER_GENERAL
    split_early_sessions();split_early_refusals();split_early_work();split_early_source_refusals();
    split_early_report();split_long_sessions();
#endif
    printf("Split controller host checks: %u\n",checks);return 0;
}
