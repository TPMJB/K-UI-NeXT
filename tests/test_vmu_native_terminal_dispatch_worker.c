/* SPDX-License-Identifier: GPL-3.0-only */
/* Real worker/heap/storage integration. Original-image structural discovery
 * has a separate corpus proof; this fixture supplies its typed contract. */
#define main persistent_suite_main
#include "test_vmu_native_persistent.c"
#undef main

#define TERMINAL_COMMON UINT32_C(0x8c013602)
#define TERMINAL_TABLE UINT32_C(0x8c013800)
static unsigned terminal_cases;
static unsigned gate_cases;
static void terminal_setup(void) {
    model_reset();fixture();prepare_handoff();model_card.bus.transfer_block=block_transfer;
    lease_calls=block_callbacks=0u;corrupt_before_loan=corrupt_after_loan=0u;
    input.exits.family=KUI_VMU_NATIVE_EXIT_TERMINAL_DISPATCH;
    input.exits.entries[2]=TERMINAL_COMMON;
    input.exits.terminal_dispatch.table=TERMINAL_TABLE;
    for(unsigned i=0u;i<4u;i++)
        put(TERMINAL_TABLE+i*4u,4u,TERMINAL_COMMON+kui_vmu_native_terminal_dispatch_entries[i]);
    for(unsigned i=0u;i<8u;i++) {
        const struct kui_vmu_native_terminal_dispatch_word *w=&kui_vmu_native_terminal_dispatch_words[i];
        put(TERMINAL_COMMON+w->offset,4u,w->value);
    }
    arm();install_hooks();
    CHECK(kui_vmu_native_terminal_dispatch_live(&config.exits,read_heap_raw,NULL,config.low_terminal));
}
static void select_terminal(void) {
    put(config.lease.owner.dispatch_cell,4u,TERMINAL_TABLE);
}
static void refused_raw(uint32_t raw,unsigned index) {
    CHECK(!poll_lease_valid());
    CHECK(state.runtime_result==(0x50003u|(index<<8)) &&
        state.fault_at==lifetime_plan.identity[index].address &&
        state.fault_value==raw && state.fault_expect==lifetime_plan.identity[index].value);
    CHECK(!lease_valid());
    CHECK(!sd.bytes && !sd.writes && !lease_calls && !block_callbacks && !durable_card.commits);
    ++terminal_cases;
}
static void allowed_routes_and_scoped_storage(void) {
    terminal_setup();CHECK(poll_lease_valid() && lease_valid());
    select_terminal();uint32_t value;
    CHECK(read_heap_raw(NULL,config.lease.owner.dispatch_cell,&value) && value==TERMINAL_TABLE);
    CHECK(read_heap(NULL,config.lease.owner.dispatch_cell,&value) && value==config.lease.owner.dispatch_table);
    CHECK(poll_lease_valid() && lease_valid() && installed_hooks_valid());
    transfer_batch(1u,0u,0u,NULL);read_compare(42u,sd.media[image_lba(42u)]);
    CHECK(durable_card.reads && lease_calls && !durable_card.commits);idle();
    ++terminal_cases;
}
static void live_capability_mutations(void) {
    terminal_setup();select_terminal();
    for(unsigned i=0u;i<4u;i++) for(unsigned bit=0u;bit<32u;bit++) {
        uint32_t at=TERMINAL_TABLE+i*4u,original=value_at(at);
        put(at,4u,original^(UINT32_C(1)<<bit));refused_raw(TERMINAL_TABLE,3u);put(at,4u,original);
    }
    for(unsigned i=0u;i<8u;i++) {
        const struct kui_vmu_native_terminal_dispatch_word *w=&kui_vmu_native_terminal_dispatch_words[i];
        for(unsigned bit=0u;bit<32u;bit++) if(w->mask&(UINT32_C(1)<<bit)) {
            uint32_t at=TERMINAL_COMMON+w->offset,original=value_at(at);
            put(at,4u,original^(UINT32_C(1)<<bit));refused_raw(TERMINAL_TABLE,3u);put(at,4u,original);
        }
    }
    for(unsigned byte=0u;byte<12u;byte++) for(unsigned bit=0u;bit<8u;bit++) {
        uint32_t at=TERMINAL_COMMON+byte;uint8_t original=memory[physical(at)-0x0c000000u];
        put(at,1u,original^(1u<<bit));refused_raw(TERMINAL_TABLE,3u);put(at,1u,original);
    }
    CHECK(poll_lease_valid() && lease_valid());
}
static void other_identity_and_unknown_routes(void) {
    for(unsigned i=0u;i<8u;i++) if(i!=3u) {
        terminal_setup();select_terminal();uint32_t at=lifetime_plan.identity[i].address;
        uint32_t raw=lifetime_plan.identity[i].value^1u;put(at,4u,raw);refused_raw(raw,i);
    }
    terminal_setup();put(config.lease.owner.dispatch_cell,4u,TERMINAL_TABLE+4u);
    refused_raw(TERMINAL_TABLE+4u,3u);
    terminal_setup();select_terminal();config.exits.family=KUI_VMU_NATIVE_EXIT_PAIR_ONLY;
    refused_raw(TERMINAL_TABLE,3u);
    terminal_setup();select_terminal();
    uint32_t first=input.lease.begin;put(first+4u,4u,UINT32_MAX);
    CHECK(poll_lease_valid() && !lease_valid()); /* Full free-list proof remains required. */
    ++terminal_cases;
}
static void cold_shape_refusals(void) {
    for(unsigned i=0u;i<6u;i++) {
        terminal_setup();struct kui_vmu_native_passive_config bad=input;
        if(i==0u)bad.exits.count=2u;
        else if(i==1u)bad.exits.terminal_dispatch.table=0u;
        else if(i==2u)bad.exits.terminal_dispatch.table|=2u;
        else if(i==3u)bad.exits.terminal_dispatch.table=bad.exits.load+bad.exits.image_bytes-12u;
        else if(i==4u)bad.exits.entries[2]=bad.exits.load+bad.exits.image_bytes-12u;
        else bad.exits.load=UINT32_C(0xec010000);
        state.armed=0u;live_sp=SELF_ADDRESS+WORKER_SPAN-0x100u;
        CHECK(kui_vmu_native_passive_arm(&bad,NULL)==-3);++terminal_cases;
    }
}
/* Compare the old unconditional call with the actual new leaf gate. The
 * full normalizer remains the reference; neither path gets a substitute
 * capability decision. Compare callback order/count at each failed read too. */
static void gate_compare(uint32_t address,uint32_t raw) {
    uint32_t expected=raw,actual=raw;
    bus_reads=0u;fail_read=0u;normalize_heap_dispatch(address,&expected);
    unsigned total=bus_reads;
    bus_reads=0u;normalize_heap_dispatch_if_routing(address,&actual);
    CHECK(actual==expected && bus_reads==total);++gate_cases;
    for(unsigned failure=1u;failure<=total;failure++) {
        expected=actual=raw;
        bus_reads=0u;fail_read=failure;normalize_heap_dispatch(address,&expected);
        unsigned attempted=bus_reads;
        bus_reads=0u;normalize_heap_dispatch_if_routing(address,&actual);
        CHECK(actual==expected && bus_reads==attempted);++gate_cases;
    }
    fail_read=0u;bus_reads=0u;
    normalize_heap_dispatch(address,NULL);
    CHECK(!bus_reads);normalize_heap_dispatch_if_routing(address,NULL);CHECK(!bus_reads);
}
static void gated_normalizer_differential(void) {
    terminal_setup();select_terminal();
    const struct kui_vmu_native_exit saved=config.exits;
    uint32_t cell=config.lease.owner.dispatch_cell;
    const uint32_t families[]={0u,1u,2u,3u,4u,5u,6u,UINT32_MAX};
    const uint32_t addresses[]={cell,cell-4u,cell-1u,cell+1u,cell+2u,cell+4u,
        cell^UINT32_C(0x20000000),physical(cell),0u,1u,UINT32_MAX-3u,UINT32_MAX};
    const uint32_t values[]={0u,config.lease.owner.dispatch_table,TERMINAL_TABLE,
        TERMINAL_TABLE+4u,TERMINAL_TABLE^UINT32_C(0x20000000),UINT32_MAX};
    for(unsigned f=0u;f<sizeof(families)/sizeof(families[0]);f++) {
        config.exits.family=families[f];
        for(unsigned a=0u;a<sizeof(addresses)/sizeof(addresses[0]);a++)
            for(unsigned v=0u;v<sizeof(values)/sizeof(values[0]);v++)
                gate_compare(addresses[a],values[v]);
    }
    config.exits=saved;
    /* An eligible routing word still invokes every original live guard.
     * Bad table alignment, wrapped extents and broken installed wrappers
     * must retain the same raw result and first failed callback. */
    for(unsigned mutation=0u;mutation<7u;mutation++) {
        config.exits=saved;
        if(mutation==0u)config.exits.count=2u;
        else if(mutation==1u)config.exits.terminal_dispatch.table=0u;
        else if(mutation==2u)config.exits.terminal_dispatch.table|=2u;
        else if(mutation==3u)config.exits.image_bytes=UINT32_MAX;
        else if(mutation==4u)config.exits.load=UINT32_C(0xfffffff0);
        else if(mutation==5u)config.exits.entries[2]+=2u;
        else config.exits.terminal_dispatch.table=UINT32_MAX-3u;
        gate_compare(cell,config.exits.terminal_dispatch.table);
    }
    config.exits=saved;
    for(unsigned i=0u;i<4u;i++) {
        uint32_t at=TERMINAL_TABLE+4u*i,original=value_at(at);
        put(at,4u,original^1u);gate_compare(cell,TERMINAL_TABLE);put(at,4u,original);
    }
    for(unsigned i=0u;i<6u;i++) {
        uint32_t at=TERMINAL_COMMON+2u*i;
        uint8_t original=memory[physical(at)-0x0c000000u];
        put(at,1u,original^1u);gate_compare(cell,TERMINAL_TABLE);put(at,1u,original);
    }
    /* The two production readers share the same gate. Preserve initial-read
     * failure and successful normalization in the proved-RAM contract. */
    for(unsigned reader=0u;reader<2u;reader++) {
        bool (*read)(void *,uint32_t,uint32_t *)=reader?read_heap_proved:read_heap;
        uint32_t expected=0u;bus_reads=fail_read=0u;
        CHECK(read(NULL,cell,&expected) && expected==config.lease.owner.dispatch_table);
        unsigned total=bus_reads;
        for(unsigned failure=1u;failure<=total;failure++) {
            uint32_t actual=UINT32_C(0x13579bdf),reference=actual;
            bus_reads=0u;fail_read=failure;
            bool old_ok=read_heap_raw(NULL,cell,&reference);
            if(old_ok)normalize_heap_dispatch(cell,&reference);
            unsigned attempted=bus_reads;
            bus_reads=0u;bool new_ok=read(NULL,cell,&actual);
            CHECK(new_ok==old_ok && actual==reference && bus_reads==attempted);++gate_cases;
        }
    }
    fail_read=bus_reads=0u;
    CHECK(!sd.bytes && !sd.writes && !lease_calls && !block_callbacks && !durable_card.commits);
}
int main(void) {
    allowed_routes_and_scoped_storage();live_capability_mutations();
    other_identity_and_unknown_routes();cold_shape_refusals();
    gated_normalizer_differential();
    printf("Terminal dispatch worker: %u checks, %u route/mutation cases; raw unknown failures, exact live fatal hook, full free-list and scoped storage preserved\n",checks,terminal_cases);
    printf("Leaf routing gate: %u differential family/address/value/read-failure cases\n",gate_cases);
    return 0;
}
