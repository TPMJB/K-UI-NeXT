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
static void uniform_warm(void) {
    model_reset();fixture();prepare_handoff();model_card.bus.transfer_block=block_transfer;
    lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
    struct kui_vmu_native_layout *l=&input.layout;
    l->rx_layout=KUI_VMU_NATIVE_RX_UNIFORM48;l->inputs=1u;
    memset(l->input_values,0,sizeof(l->input_values));memset(l->indices,0,sizeof(l->indices));
    l->input_values[0]=0xac500004u;l->indices[0]=0xac400000u;l->rx_base=0xac500020u;
    put(l->input_array,4u,l->input_values[0]);put(l->index_array,4u,l->indices[0]);
    put(0x0c300fc8u,4u,l->rx_base);
    for(unsigned i=0;i<48u;i++) {l->rx_slots[i]=l->rx_base+i*1024u;
        put(0x0c300fe8u+i*4u,4u,l->rx_slots[i]);}
    arm();install_hooks();transfer_batch(0u,0u,0u,NULL);
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
            0x033d0008u|(port<<14)|(port<<22));
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
    memcpy(before[4],rx_bytes(2u),1024u);
}
static void same_rx(uint8_t before[5][1024]) {
    for(unsigned port=0;port<4u;port++) CHECK(!memcmp(before[port],rx_bytes(port*6u),1024u));
    CHECK(!memcmp(before[4],rx_bytes(2u),1024u));
}
static void save_all_rx(uint8_t before[KUI_VMU_NATIVE_RX_SLOTS][1024]) {
    for(unsigned i=0u;i<KUI_VMU_NATIVE_RX_SLOTS;i++) if(config.runtime.rx_slots[i])
        memcpy(before[i],readonly_rx(physical(config.runtime.rx_slots[i]),1024u),1024u);
}
static void same_all_rx(uint8_t before[KUI_VMU_NATIVE_RX_SLOTS][1024]) {
    for(unsigned i=0u;i<KUI_VMU_NATIVE_RX_SLOTS;i++) if(config.runtime.rx_slots[i])
        CHECK(!memcmp(before[i],readonly_rx(physical(config.runtime.rx_slots[i]),1024u),1024u));
}
static void no_storage(void) {
    CHECK(!sd.bytes && !sd.writes && !lease_calls && !block_callbacks && !durable_card.commits);
    CHECK(!overlay.stats.backend_calls && !overlay.stats.replies);idle();
}
/* Exact recurring protocol words emitted by the external Evolution SDK replay
 * (ASIC2, one A controller, empty B-D). RX addresses are bound to this fixture's
 * independently proved bank slots; no executable bytes are embedded. */
static void mixed_stage(unsigned conditions,unsigned allinfo) {
    controllers(1u,0u);uint32_t source=value_at(0xa05f6c04u);
    unsigned at=0u;
    for(unsigned port=0u;port<4u;port++) {
        unsigned command=(conditions&(1u<<port))?9u:(allinfo&(1u<<port))?2u:1u;
        unsigned words=command==9u?1u:0u;
        put(source+at,4u,(port<<16)|words|(port==3u?0x80000000u:0u));
        put(source+at+4u,4u,rx_at(port*6u));
        put(source+at+8u,4u,(words<<24)|(port<<22)|(port<<14)|0x2000u|command);
        if(words) put(source+at+12u,4u,0x01000000u);
        memset(rx_bytes(port*6u),0xa5,1024u);at+=12u+4u*words;
    }
    if(conditions==1u && !allinfo) {
        uint32_t original[]={0x00000001u,0u,0x01002009u,0x01000000u,
            0x00010000u,0u,0x00406001u,0x00020000u,0u,0x0080a001u,
            0x80030000u,0u,0x00c0e001u};
        original[1]=rx_at(0u);original[5]=rx_at(6u);
        original[8]=rx_at(12u);original[11]=rx_at(18u);
        CHECK(at==sizeof(original) && !memcmp(original,memory+source-0x0c000000u,sizeof(original)));
    }
}
static void mixed_done(unsigned conditions,unsigned allinfo,unsigned attached,const uint32_t functions[4]) {
    physical_complete();
    for(unsigned port=0u;port<4u;port++) {
        if(!(attached&(1u<<port))) {put(rx_at(port*6u),4u,UINT32_MAX);continue;}
        unsigned command=(conditions&(1u<<port))?9u:(allinfo&(1u<<port))?2u:1u;
        unsigned words=command==9u?3u:command==2u?48u:28u;
        unsigned reply=command==9u?8u:command==2u?6u:5u;
        put(rx_at(port*6u),4u,(words<<24)|(port<<22)|(port<<14)|0x003d0000u|reply);
        put(rx_at(port*6u)+4u,4u,functions[port]);
    }
}
static void mixed_begin(unsigned conditions,unsigned allinfo) {
    mixed_stage(conditions,allinfo);submit();
    CHECK(controller_pair.pending && !overlay.pending && state.descriptors==4u);
    CHECK(value_at(0xa05f6c04u)==state.source);put(0xa05f6c18u,4u,1u);
}
static void mixed_queries_and_cost(void) {
    const uint32_t functions[4]={0x01000000u,0x01000000u,0x40000000u,0x04000000u};
    unsigned fast=0u,full=0u;
    for(unsigned layout=0u;layout<2u;layout++) for(unsigned allinfo=0u;allinfo<8u;allinfo++) {
        if(layout) uniform_warm();else warm();
        for(unsigned attached=1u;attached<16u;attached+=2u) {
            uint8_t before[5][1024],all_before[KUI_VMU_NATIVE_RX_SLOTS][1024],original[0x3000];
            mixed_stage(1u,allinfo<<1);
            uint32_t source=value_at(0xa05f6c04u);
            memcpy(original,memory+source-0x0c000000u,sizeof(original));
            submit();CHECK(controller_pair.pending && !overlay.pending);
            unsigned submit_reads=measured_submit_reads;
            put(0xa05f6c18u,4u,1u);mixed_done(1u,allinfo<<1,attached,functions);
            save_rx(before);save_all_rx(all_before);complete_return();
            before[0][2]|=2u;same_rx(before);no_storage();
            all_before[packet_bank*24u][2]|=2u;same_all_rx(all_before);
            CHECK(!memcmp(original,memory+source-0x0c000000u,sizeof(original)));
            CHECK(value_at(0xa05f6c04u)==source && !controller_pair.pending && !overlay.pending);
            if(layout && !allinfo && attached==1u) fast=submit_reads+measured_complete_reads;
        }
    }
    /* Newly connected B is enumerated physically, then its ordinary polling
     * can share the lane with still-empty or noncontroller C/D queries. */
    warm();mixed_begin(1u,0u);mixed_done(1u,0u,3u,functions);complete_return();
    mixed_begin(3u,0u);mixed_done(3u,0u,15u,functions);complete_return();no_storage();
    /* PROFILE uses the same wire fast lane. Timing/gesture bookkeeping is
     * shared by its caller and checked by the persistent diagnostic suite. */
    uniform_warm();config.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_PROFILE;
    mixed_begin(1u,0u);mixed_done(1u,0u,1u,functions);complete_return();no_storage();
    /* Full reference uses identical A advertisement semantics and byte-for-
     * byte output; host tracing is compiled out of this fixture. */
    uniform_warm();config.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE;
    mixed_stage(1u,0u);uint8_t full_original[0x3000];uint32_t full_source=value_at(0xa05f6c04u);
    memcpy(full_original,memory+full_source-0x0c000000u,sizeof(full_original));
    submit();CHECK(!controller_pair.pending && overlay.pending);
    unsigned submit_reads=measured_submit_reads;put(0xa05f6c18u,4u,1u);
    mixed_done(1u,0u,1u,functions);uint8_t before[5][1024],all_before[KUI_VMU_NATIVE_RX_SLOTS][1024];
    save_rx(before);save_all_rx(all_before);
    complete_return();before[0][2]|=2u;same_rx(before);no_storage();
    all_before[packet_bank*24u][2]|=2u;same_all_rx(all_before);
    CHECK(!memcmp(full_original,memory+full_source-0x0c000000u,sizeof(full_original)));
    full=submit_reads+measured_complete_reads;
    CHECK(fast<full);
    printf("Actual recurring A GETCOND+empty B-D DEVINFO wire, uniform layout: fast=%u full=%u guest reads/pair\n",fast,full);
    /* ASIC0's original constructor enables only A-C. Its recurring wire is
     * the same prefix with C marked LAST: ten words /40 bytes. D is not read
     * or rewritten by the worker, and both hardware banks remain coherent. */
    uniform_warm();
    for(unsigned repeat=0u;repeat<4u;repeat++) {
        mixed_stage(1u,0u);uint32_t source=value_at(0xa05f6c04u);
        put(source+28u,4u,0x80020000u);
        uint8_t original[0x3000],all_rx[KUI_VMU_NATIVE_RX_SLOTS][1024];
        memcpy(original,memory+source-0x0c000000u,sizeof(original));
        submit();CHECK(controller_pair.pending && state.descriptors==3u && state.bytes==40u);
        CHECK(controller_pair.words==10u && value_at(0xa05f6c04u)==source);
        put(0xa05f6c18u,4u,1u);physical_complete();
        put(rx_at(6u),4u,UINT32_MAX);put(rx_at(12u),4u,UINT32_MAX);
        save_all_rx(all_rx);complete_return();all_rx[packet_bank*24u][2]|=2u;
        same_all_rx(all_rx);no_storage();
        CHECK(!memcmp(original,memory+source-0x0c000000u,sizeof(original)));
    }
}
static void mixed_query_refusals(void) {
    const uint32_t functions[4]={0x01000000u,0x01000000u,0x40000000u,0x04000000u};
    for(unsigned allinfo=0u;allinfo<2u;allinfo++) for(unsigned mutation=0u;mutation<7u;mutation++) {
        warm();mixed_begin(1u,allinfo?14u:0u);mixed_done(1u,allinfo?14u:0u,15u,functions);
        uint32_t address=rx_at(18u),header=value_at(address);
        switch(mutation) {
        case 0:put(address,4u,header^0x100u);break;
        case 1:put(address,4u,header^0x200000u);break;
        case 2:put(address,4u,header^0x1000000u);break;
        case 3:put(address,4u,header^1u);break;
        case 4:put(address+4u,4u,0u);break;
        case 5:put(state.source+48u,4u,value_at(state.source+48u)^1u);break;
        default:rx_bytes(0u)[2]|=2u;break;
        }
        uint8_t before[5][1024];save_rx(before);reject_complete();same_rx(before);no_storage();
    }
    /* Every completion read of the proved recurring mixed batch is mandatory
     * and fails before the sole advertisement store. */
    warm();mixed_begin(1u,0u);mixed_done(1u,0u,1u,functions);complete_return();
    unsigned reads=measured_complete_reads;
    for(unsigned fault=1u;fault<=reads;fault++) {
        uint8_t before[5][1024];warm();mixed_begin(1u,0u);mixed_done(1u,0u,1u,functions);
        save_rx(before);bus_reads=0u;fail_read=fault;reject_complete();
        CHECK(bus_reads==fault);same_rx(before);no_storage();
    }
}
static void mixed_query_errors(void) {
    const uint32_t functions[4]={0x01000000u,0x01000000u,0x40000000u,0x04000000u};
    for(unsigned command=1u;command<=2u;command++) for(unsigned port=1u;port<4u;port++)
        for(unsigned error=0xfcu;error<=0xfeu;error++) for(unsigned mutation=0u;mutation<4u;mutation++) {
            warm();mixed_begin(1u,command==2u?14u:0u);
            mixed_done(1u,command==2u?14u:0u,15u,functions);
            uint32_t response=0x00200000u|(port<<14)|(port<<22)|error;
            if(mutation==1u) response|=0x01000000u;
            else if(mutation==2u) response^=0x100u;
            else if(mutation==3u) response^=0x200000u;
            put(rx_at(port*6u),4u,response);put(rx_at(port*6u)+4u,4u,0u);
            uint8_t before[5][1024];save_rx(before);
            if(mutation) reject_complete();else {complete_return();before[0][2]|=2u;}
            same_rx(before);no_storage();
        }
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
            before[0][2]|=2u;same_rx(before);no_storage();
            CHECK(!memcmp(original,memory+source-0x0c000000u,sizeof(original)));
        }
        CHECK(overlay.stats.prepared==5u && overlay.stats.completed==5u && overlay.stats.advertisements==5u);
    }
    /* Trace is a full-path mode with the same parent advertisement. The host
     * fixture compiles trace callbacks out, so this compares only VMU work. */
    warm();controllers(15u,1u);config.diagnostics=KUI_VMU_NATIVE_DIAGNOSTICS_EVOLUTION_TRACE;
    submit();CHECK(!controller_pair.pending && overlay.pending);unsigned full_submit=measured_submit_reads;
    put(0xa05f6c18u,4u,1u);controllers_done(15u,0u);
    uint8_t full_rx[5][1024];save_rx(full_rx);complete_return();full_rx[0][2]|=2u;same_rx(full_rx);
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
        if(which==0u) { /* Parent enumeration retains the complete path. */
            controllers(1u,0u);uint32_t source=value_at(0xa05f6c04u);
            put(source,4u,0x80000000u);put(source+8u,4u,0x00002001u);
        }
        else if(which==1u) stage_batch(1u,0u); /* A2 enumeration retains full backend. */
        else if(which==2u) stage_batch(11u,42u); /* A2 read retains storage authority. */
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
static unsigned diagnostic_cases;
static void witness(unsigned family,unsigned reason,unsigned index,uint32_t address,
    uint32_t value,uint32_t expected) {
    CHECK(state.runtime_result==(family|(index<<8)|reason));
    CHECK(state.fault_at==address && state.fault_value==value && state.fault_expect==expected);
    ++diagnostic_cases;
}
static void reject_controller_direct(void) {
    if(setjmp(terminal)==0) {controller_complete(&frame);CHECK(false);}
    CHECK(terminal_error==0x47u);
}
static void reject_submit_after_warm(void) {
    if(setjmp(terminal)==0) {kui_vmu_native_passive_submit(&frame);CHECK(false);}
    CHECK(terminal_error==3u && !state.pending);
}
static void failure_witness_cases(void) {
    /* Aggregate guards retain every independently invalid bit. */
    const uint32_t masks[13]={1u,2u,0x84u,8u,0x50u,0x20u,0x40u,0x80u,
        0x100u,0x200u,0x400u,0x800u,0x1000u};
    for(unsigned i=0u;i<13u;i++) {
        uint8_t before[5][1024];warm();begin_controllers(15u,1u);controllers_done(15u,0u);
        switch(i) {
        case 0u:controller_pair.pending=0u;break;
        case 1u:controller_pair.source^=32u;break;
        case 2u:controller_pair.words=0u;break;
        case 3u:controller_pair.words=33u;state.bytes=132u;break;
        case 4u:controller_pair.count=state.descriptors=0u;break;
        case 5u:controller_pair.count=state.descriptors=5u;break;
        case 6u:controller_pair.parent=controller_pair.count;break;
        case 7u:state.bytes^=4u;break;
        case 8u:++state.descriptors;break;
        case 9u:overlay.pending=1u;break;
        case 10u:overlay.admitted=0u;break;
        case 11u:durable_card.ready=0u;break;
        default:durable_card.faulted=1u;break;
        }
        save_rx(before);
        reject_controller_direct();same_rx(before);no_storage();
        witness(0x40000u,1u,0u,(uint32_t)(uintptr_t)&controller_pair,masks[i],0u);
    }
    /* A physical A1 VMU uses bit0; only physical A2's bit1 conflicts. */
    for(unsigned a2=0u;a2<2u;a2++) {
        uint8_t before[5][1024];warm();begin_controllers(1u,0u);controllers_done(1u,0u);
        uint32_t response=0x03210008u|(a2<<17);put(rx_at(0u),4u,response);save_rx(before);
        if(a2) {
            reject_complete();witness(0x40000u,10u,0u,rx_at(0u)|0xa0000000u,response,0x03210008u);
        } else {complete_return();before[0][2]|=2u;}
        same_rx(before);no_storage();
    }
    /* The fixed caller's exact identity predicate is unchanged; every first
     * failing read or compare gets fresh fields, including dispatch index3. */
    for(unsigned i=0u;i<8u;i++) for(unsigned unreadable=0u;unreadable<2u;unreadable++) {
        warm();uint32_t address=lifetime_plan.identity[i].address;
        uint32_t expected=lifetime_plan.identity[i].value;
        state.fault_at=state.fault_value=state.fault_expect=state.runtime_result=0xdeadbeefu;
        if(unreadable) {bus_reads=0u;fail_read=i+1u;}
        else put(address,4u,expected^1u);
        CHECK(!poll_lease_valid());
        if(unreadable) {CHECK(bus_reads==i+1u);fail_read=0u;}
        witness(0x50000u,unreadable?2u:3u,i,address,unreadable?UINT32_MAX:expected^1u,expected);
        no_storage();
    }
    for(unsigned i=0u;i<3u;i++) {
        warm();
        if(i==0u)lifetime_plan.self^=UINT32_C(0x20000000);
        else if(i==1u)lifetime_plan.ready^=1u;
        else lifetime_plan.count=KUI_VMU_NATIVE_LIFETIME_RANGES+1u;
        unsigned before=bus_reads;CHECK(!poll_lease_valid() && bus_reads==before);
        witness(0x50000u,1u,0u,(uint32_t)(uintptr_t)&lifetime_plan,1u<<i,0u);no_storage();
    }
    for(unsigned i=0u;i<3u;i++) {
        warm();controllers(1u,0u);
        if(i!=1u)kui_vmu_native_passive_resume^=2u;
        if(i)kui_vmu_native_passive_consumer^=2u;
        reject_submit_after_warm();
        unsigned reason=i==1u?5u:4u;
        witness(0x50000u,reason,0u,(uint32_t)(uintptr_t)(i==1u?
            &kui_vmu_native_passive_consumer:&kui_vmu_native_passive_resume),
            i==1u?kui_vmu_native_passive_consumer:kui_vmu_native_passive_resume,
            i==1u?config.driver.consumer:config.driver.resume);no_storage();
    }
}
static void exact_response_and_read_witnesses(void) {
    for(unsigned i=0u;i<6u;i++) {
        uint8_t before[5][1024];warm();begin_controllers(1u,0u);controllers_done(1u,0u);
        uint32_t address=rx_at(0u)|0xa0000000u,actual,expected,reason;
        if(i==0u) {reason=6u;actual=0x033d0009u;expected=0x03200008u;put(address,4u,actual);}
        else if(i==1u) {reason=8u;actual=0x02000000u;expected=0x01000000u;address+=4u;put(address,4u,actual);}
        else if(i==2u) {reason=9u;actual=UINT32_MAX;expected=0u;put(address,4u,actual);}
        else if(i==3u) {reason=4u;actual=UINT32_MAX;expected=0u;bus_reads=0u;fail_read=controller_pair.words+1u;}
        else if(i==4u) {reason=7u;actual=UINT32_MAX;expected=0x01000000u;address+=4u;bus_reads=0u;fail_read=controller_pair.words+2u;}
        else {reason=11u;actual=0u;expected=1024u;address=rx_at(0u);}
        save_rx(before);if(i==5u)refuse_rx_map=true;
        reject_controller_direct();fail_read=0u;refuse_rx_map=false;
        witness(0x40000u,reason,0u,address,actual,expected);same_rx(before);no_storage();
    }
    for(unsigned word=0u;word<17u;word++) {
        uint8_t before[5][1024];warm();begin_controllers(15u,1u);controllers_done(15u,0u);save_rx(before);
        uint32_t expected=controller_pair.snapshot[word];bus_reads=0u;fail_read=word+1u;
        reject_controller_direct();CHECK(bus_reads==word+1u);fail_read=0u;
        witness(0x40000u,2u,word,(state.source+word*4u)|0xa0000000u,UINT32_MAX,expected);
        same_rx(before);no_storage();
    }
    const uint32_t functions[4]={0x01000000u,0x01000000u,0x40000000u,0x04000000u};
    for(unsigned i=0u;i<2u;i++) {
        uint8_t before[5][1024];warm();mixed_begin(1u,0u);mixed_done(1u,0u,15u,functions);
        uint32_t address=rx_at(18u)|0xa0000000u,actual,expected,reason;
        if(!i) {reason=5u;actual=0x01e0c0fcu;expected=0x00e0c0fcu;put(address,4u,actual);}
        else {reason=8u;actual=0u;expected=1u;address+=4u;put(address,4u,actual);}
        save_rx(before);reject_controller_direct();
        witness(0x40000u,reason,3u,address,actual,expected);same_rx(before);no_storage();
    }
    /* The success path does not turn these four fields into live telemetry. */
    warm();begin_controllers(1u,0u);controllers_done(1u,0u);
    state.fault_at=1u;state.fault_value=2u;state.fault_expect=3u;state.runtime_result=4u;
    complete_return();CHECK(state.fault_at==1u && state.fault_value==2u &&
        state.fault_expect==3u && state.runtime_result==4u);no_storage();
}
static void refused_without_publication(void) {
    for(unsigned which=0;which<28u;which++) {
        uint8_t before[5][1024];warm();begin_controllers(15u,1u);controllers_done(15u,0u);
        switch(which) {
        case 0:put(state.source,4u,0x701u);break;
        case 1:put(state.source+4u,4u,0x00010001u);break;
        case 2:put(state.source+8u,4u,rx_at(2u));break;
        case 3:put(state.source+12u,4u,0x01002001u);break;
        case 4:put(state.source+16u,4u,0x02000000u);break;
        case 5:rx_bytes(0u)[2]|=2u;break; /* Physical A2 conflict. */
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
        if(which<5u) CHECK(state.runtime_result==(0x40003u|(which<<8)));
        else if(which==5u) CHECK(state.runtime_result==0x4000au);
        else if(which==6u) CHECK(state.runtime_result==0x40009u);
        else if(which==7u) CHECK(state.runtime_result==0x40008u);
        else if(which==8u) CHECK(state.runtime_result==0x40306u);
        else if(which==9u) CHECK(state.runtime_result==0x40308u);
        else if(which==10u) CHECK(state.runtime_result==0x20020u);
        else if(which==11u) CHECK(state.runtime_result==0x20021u);
        else if(which==12u) CHECK(state.runtime_result==0x20022u);
        else if(which==13u) CHECK(state.runtime_result==0x20023u);
        else if(which==14u) CHECK(state.runtime_result==0x20042u);
        else if(which==15u) CHECK(state.runtime_result==(0x40025u|((32u+packet_bank*24u+18u)<<8)));
        else if(which==16u) CHECK(state.runtime_result==0x40825u);
        else if(which==19u) CHECK(state.runtime_result==0x2000cu);
        else if(which==20u) CHECK(state.runtime_result==0x20008u);
        else if(which==22u) CHECK(state.runtime_result==0x40526u);
        else if(which==23u) CHECK(state.runtime_result==0x20031u);
        else if(which>=24u) CHECK(state.runtime_result==0x40001u);
    }
    /* Every source word, including padding/control/LAST, is immutable. */
    for(unsigned word=0u;word<17u;word++) {
        uint8_t before[5][1024];warm();begin_controllers(15u,1u);controllers_done(15u,0u);
        uint32_t expected=value_at(state.source+word*4u);
        put(state.source+word*4u,4u,expected^1u);
        save_rx(before);reject_complete();same_rx(before);no_storage();
        witness(0x40000u,3u,word,(state.source+word*4u)|0xa0000000u,expected^1u,expected);
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
static void registered_timer_callbacks(void) {
    const uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
    for(unsigned timer=0u;timer<2u;timer++) for(unsigned a=0u;a<3u;a++) {
        warm();begin_controllers(1u,0u);
        /* Reproduce the measured Skies lifecycle: registration occurs after
         * the original null-callback bind and before a controller completion. */
        put(timer?input.layout.timer2_callback:input.layout.timer1_callback,4u,aliases[a]|0x0c087498u);
        controllers_done(1u,0u);complete_return();no_storage();
        /* The following full virtual-card/storage path must use the same
         * policy; merely patching the controller fast lane is insufficient. */
        transfer_batch(1u,0u,0u,NULL);read_compare(42u,sd.media[image_lba(42u)]);
        put(timer?input.layout.timer2_callback:input.layout.timer1_callback,4u,0u);
        begin_controllers(1u,0u);controllers_done(1u,0u);complete_return();idle();
    }
    const uint32_t invalid[]={1u,0x8c00fffeu,0x8c210000u,0x8c20ffffu,
        0xec087498u,0x6c087498u,0xa05f6c00u,0xac700000u,0x8ce00000u};
    for(unsigned timer=0u;timer<2u;timer++) for(unsigned i=0u;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        uint8_t before[5][1024];warm();begin_controllers(1u,0u);controllers_done(1u,0u);
        uint32_t address=timer?input.layout.timer2_callback:input.layout.timer1_callback;
        put(address,4u,invalid[i]);save_rx(before);reject_complete();same_rx(before);no_storage();
        CHECK(state.runtime_result==(0x40026u|((5u+timer)<<8)));
        CHECK(state.fault_at==address && state.fault_value==invalid[i] &&
            state.fault_expect==input.driver.load_address);
    }
}
int main(void) {
    success_and_cost();mixed_queries_and_cost();mixed_query_refusals();mixed_query_errors();layouts_and_live_aliases();fallback();
    refused_without_publication();lane_switches_and_lease();failure_witness_cases();exact_response_and_read_witnesses();
    registered_timer_callbacks();
    printf("Controller/main-unit query fast lane: %u checks; unchanged DMA, exact RX/header publication, full fallback and durable lane switching\n",checks);
    printf("Failure-only diagnostic witnesses: %u exact field cases; source/response/physical A1-A2/identity/exports, transfer codes preserved, no RX or SD publication on refusal\n",diagnostic_cases);
    return 0;
}
