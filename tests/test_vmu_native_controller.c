/* SPDX-License-Identifier: GPL-3.0-only */
/* The production worker and full persistent fixture run unchanged. Fast
 * polling is checked against its physical DMA/one-byte publication contract. */
#define main persistent_regression_main
#include "test_vmu_native_persistent.c"
#undef main

static void warm(void) {
    setup();transfer_batch(0u,0u,0u,NULL);
    CHECK(live_plan.ready && !controller_pair.pending && !overlay.pending);
}
static void controllers(unsigned ports,unsigned nops) {
    stage_batch(0u,0u);
    uint32_t source=value_at(0xa05f6c04u);
    memset(memory+source-0x0c000000u,0,0x3000u);
    unsigned at=0u,last=0u;
    for(unsigned i=0;i<nops;i++,at+=4u) put(source+at,4u,0x700u);
    for(unsigned port=0;port<4u;port++) if(ports&(1u<<port)) {
        memset(rx_bytes(port*6u),0xa5,1024u);last=at;
        put(source+at,4u,1u|(port<<16));put(source+at+4u,4u,rx_at(port*6u));
        put(source+at+8u,4u,0x01002009u|(port<<14)|(port<<22));
        put(source+at+12u,4u,0x01000000u);at+=16u;
    }
    put(source+last,4u,value_at(source+last)|0x80000000u);
}
static void controllers_done(unsigned ports,unsigned absent) {
    physical_complete();
    for(unsigned port=0;port<4u;port++) if(ports&(1u<<port)) {
        put(rx_at(port*6u),4u,(absent&(1u<<port))?UINT32_MAX:
            0x033e0008u|(port<<14)|(port<<22));
        put(rx_at(port*6u)+4u,4u,0x01000000u);
    }
}
static void begin_controllers(unsigned ports,unsigned nops) {
    controllers(ports,nops);submit();
    CHECK(controller_pair.pending && !overlay.pending);
    CHECK(value_at(0xa05f6c04u)==state.source);
    put(0xa05f6c18u,4u,1u);
}
static void save_rx(uint8_t before[5][1024]) {
    for(unsigned port=0;port<4u;port++) memcpy(before[port],rx_bytes(port*6u),1024u);
    memcpy(before[4],rx_bytes(1u),1024u);
}
static void same_rx(uint8_t before[5][1024]) {
    for(unsigned port=0;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
    CHECK(!memcmp(before[4],rx_bytes(1u),1024u));
}
static void no_storage(void) {
    CHECK(!sd.bytes && !sd.writes && !lease_calls && !block_callbacks && !durable_card.commits);
    CHECK(!overlay.stats.backend_calls && !overlay.stats.replies);idle();
}
static void success_and_cost(void) {
    unsigned fast_submit=0u,fast_complete=0u,full=0u;
    for(unsigned ports=1u;ports<16u;ports+=2u) for(unsigned nops=0;nops<2u;nops++) {
        warm();
        for(unsigned repeat=0;repeat<4u;repeat++) {
            uint8_t original[0x3000],before[5][1024];controllers(ports,nops);
            uint32_t source=value_at(0xa05f6c04u);
            memcpy(original,memory+source-0x0c000000u,sizeof(original));
            submit();CHECK(controller_pair.pending && !overlay.pending);
            fast_submit=measured_submit_reads;
            CHECK(value_at(0xa05f6c04u)==source);
            CHECK(!memcmp(original,memory+source-0x0c000000u,sizeof(original)));
            put(0xa05f6c18u,4u,1u);controllers_done(ports,repeat&2u?ports&~1u:0u);
            save_rx(before);complete_return();fast_complete=measured_complete_reads;
            CHECK(!controller_pair.pending && !overlay.pending);
            CHECK(value_at(0xa05f6c04u)==source);
            before[0][2]|=1u;same_rx(before);no_storage();
            CHECK(!memcmp(original,memory+source-0x0c000000u,sizeof(original)));
        }
        CHECK(overlay.stats.prepared==5u && overlay.stats.completed==5u && overlay.stats.advertisements==5u);
    }
    /* Trace is a full-path mode with the same parent advertisement. The host
     * fixture compiles trace callbacks out, so this compares only VMU work. */
    warm();controllers(15u,1u);config.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE;
    submit();CHECK(!controller_pair.pending && overlay.pending);unsigned full_submit=measured_submit_reads;
    put(0xa05f6c18u,4u,1u);controllers_done(15u,0u);
    uint8_t full_rx[5][1024];save_rx(full_rx);complete_return();full_rx[0][2]|=1u;same_rx(full_rx);
    full=full_submit+measured_complete_reads;no_storage();
    CHECK(fast_submit+fast_complete<full);
    printf("Controller-only guest reads: submit=%u complete=%u pair=%u; same4-controller full path=%u\n",
        fast_submit,fast_complete,fast_submit+fast_complete,full);
    /* The bounded maximum includes16 SDK padding words and4 complete frames. */
    warm();begin_controllers(15u,16u);controllers_done(15u,0u);complete_return();no_storage();
}
static void layouts_and_live_aliases(void) {
    const uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
    for(unsigned geometry=0u;geometry<2u;geometry++) for(unsigned alias=0u;alias<3u;alias++) {
        setup();
        if(geometry) {
            /* Re-arm in final private storage with the alternate production
             * initializer layout; this is not a fabricated hot proof. */
            state.armed=0u;fixture();prepare_handoff();model_card.bus.transfer_block=block_transfer;
            struct kui_vmu_native_layout *l=&input.layout;
            l->rx_layout=KUI_VMU_NATIVE_RX_UNIFORM48;l->inputs=1u;
            memset(l->input_values,0,sizeof(l->input_values));memset(l->indices,0,sizeof(l->indices));
            l->input_values[0]=0xac500004u;l->indices[0]=0xac400000u;l->rx_base=0xac500020u;
            put(l->input_array,4u,l->input_values[0]);put(l->index_array,4u,l->indices[0]);
            put(0x0c300fc8u,4u,l->rx_base);
            for(unsigned i=0;i<48u;i++) {l->rx_slots[i]=l->rx_base+i*1024u;
                put(0x0c300fe8u+i*4u,4u,l->rx_slots[i]);}
            arm();install_hooks();
        }
        transfer_batch(0u,0u,0u,NULL);
        begin_controllers(15u,1u);controllers_done(15u,0u);
        put(input.driver.manager_cell,4u,physical(input.driver.manager_initial)|aliases[alias]);
        complete_return();no_storage();
        begin_controllers(15u,1u);controllers_done(15u,0u);
        put(input.driver.manager_cell,4u,physical(input.driver.manager_initial)|aliases[(alias+1u)%3u]);
        put(0x0c300fe8u+(packet_bank*24u+18u)*4u,4u,0u);
        uint8_t before[5][1024];save_rx(before);reject_complete();same_rx(before);no_storage();
    }
    /* An unrelated RX slot is outside the one-byte fast-lane authority. The
     * complete VMU path must still reject that same changed table entry. */
    warm();begin_controllers(1u,0u);controllers_done(1u,0u);
    put(0x0c300fe8u+(packet_bank*24u+1u)*4u,4u,0u);
    complete_return();no_storage();
    stage_batch(11u,42u);uint8_t before[5][1024];save_rx(before);
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
    CHECK(terminal_error && !state.pending && !controller_pair.pending);
    same_rx(before);no_storage();
}
static void fallback(void) {
    for(unsigned which=0;which<6u;which++) {
        warm();
        if(which==0u) stage_batch(0u,0u); /* B DEVINFO: whole batch takes full path. */
        else if(which==1u) stage_batch(1u,0u); /* A1 enumeration retains full backend. */
        else if(which==2u) stage_batch(11u,42u); /* A1 read retains storage authority. */
        else if(which==3u) controllers(15u,17u); /* Valid but beyond fast bound. */
        else if(which==4u) {controllers(1u,0u);config.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_HOOKS_ONLY;}
        else {controllers(1u,0u);config.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN;}
        submit();CHECK(!controller_pair.pending && overlay.pending);
        CHECK(config.diagnostics==KUI_VMU_NATIVE_DIAGNOSTICS_RAW_CHAIN?
            value_at(0xa05f6c04u)==state.source:value_at(0xa05f6c04u)==issued_physical());
    }
    warm();controllers(2u,0u);state.source=value_at(0xa05f6c04u);
    CHECK(!controller_prepare(&frame) && !controller_pair.pending && !overlay.pending);
}
static void refused_without_publication(void) {
    for(unsigned which=0;which<28u;which++) {
        uint8_t before[5][1024];warm();begin_controllers(15u,1u);controllers_done(15u,0u);
        switch(which) {
        case 0:put(state.source,4u,0x701u);break;
        case 1:put(state.source+4u,4u,0x00010001u);break;
        case 2:put(state.source+8u,4u,rx_at(1u));break;
        case 3:put(state.source+12u,4u,0x01002001u);break;
        case 4:put(state.source+16u,4u,0x02000000u);break;
        case 5:rx_bytes(0u)[2]|=1u;break; /* Physical A1 conflict. */
        case 6:put(rx_at(0u),4u,UINT32_MAX);break;
        case 7:put(rx_at(0u)+4u,4u,0x02000000u);break;
        case 8:put(rx_at(18u),4u,0x03e00009u);break; /* Last descriptor: still no earlier write. */
        case 9:put(rx_at(18u)+4u,4u,0u);break;
        case 10:put(0xa05f6c04u,4u,state.source+32u);break;
        case 11:put(0xa05f6c10u,4u,1u);break;
        case 12:put(0xa05f6c14u,4u,0u);break;
        case 13:put(0xa05f6c18u,4u,1u);break;
        case 14:put(0x0c3010b4u,4u,packet_bank);break;
        case 15:put(0x0c300fe8u+(packet_bank*24u+18u)*4u,4u,input.layout.rx_slots[18u]);break;
        case 16:put(input.layout.input_array,4u,0u);break;
        case 17:put(input.lease.header+4u,4u,0u);break;
        case 18:put(input.driver.completion_slot,4u,0u);break;
        case 19:frame.pr+=2u;break;
        case 20:frame.sr=0x50u;break;
        case 21:bad_guard=1u;break;
        case 22:put(input.layout.timer1_callback,4u,1u);break;
        case 23:put(input.driver.lock_cell,4u,0u);break;
        case 24:controller_pair.count=state.descriptors=CONTROLLER_FRAMES+1u;break;
        case 25:controller_pair.parent=controller_pair.count;break;
        case 26:controller_pair.words=CONTROLLER_WORDS+1u;state.bytes=controller_pair.words*4u;break;
        default:controller_pair.count=state.descriptors=0u;break;
        }
        save_rx(before);reject_complete();same_rx(before);no_storage();
        CHECK(controller_pair.pending && state.submitted==state.completed+1u);
    }
    /* Every source word, including padding/control/LAST, is immutable. */
    for(unsigned word=0u;word<17u;word++) {
        uint8_t before[5][1024];warm();begin_controllers(15u,1u);controllers_done(15u,0u);
        put(state.source+word*4u,4u,value_at(state.source+word*4u)^1u);
        save_rx(before);reject_complete();same_rx(before);no_storage();
    }
    warm();begin_controllers(15u,1u);controllers_done(15u,0u);complete_return();
    unsigned reads=measured_complete_reads;
    for(unsigned fault=1u;fault<=reads;fault++) {
        uint8_t before[5][1024];warm();begin_controllers(15u,1u);controllers_done(15u,0u);
        save_rx(before);bus_reads=0u;fail_read=fault;reject_complete();
        CHECK(bus_reads==fault);same_rx(before);no_storage();
    }
}
static void lane_switches_and_lease(void) {
    uint8_t data[512];memset(data,0x47,sizeof(data));warm();
    begin_controllers(1u,0u);controllers_done(1u,0u);complete_return();
    transfer_batch(1u,0u,0u,NULL);read_compare(42u,sd.media[image_lba(42u)]);
    stage_block(42u,data);transfer_batch(13u,42u,4u,NULL);read_compare(42u,data);
    begin_controllers(15u,1u);controllers_done(15u,0u);complete_return();
    CHECK(durable_card.commits==1u && !controller_pair.pending && !overlay.pending);idle();
    warm();begin_controllers(1u,0u);frame.r4=config.lease.payload;
    if(setjmp(terminal)==0) {kui_vmu_native_readonly_release_check(&frame);CHECK(false);}
    CHECK(terminal_error==0x30u && controller_pair.pending);
}
int main(void) {
    success_and_cost();layouts_and_live_aliases();fallback();refused_without_publication();lane_switches_and_lease();
    printf("Controller-only fast lane: %u checks; unchanged DMA, exact RX/header publication, full fallback and durable lane switching\n",checks);
    return 0;
}
