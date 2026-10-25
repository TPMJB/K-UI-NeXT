/* SPDX-License-Identifier: GPL-3.0-only */
/* Real worker/heap/storage integration. Original-image structural discovery
 * has a separate corpus proof; this fixture supplies its typed contract. */
#define main persistent_suite_main
#include "test_vmu_native_persistent.c"
#undef main

#define TERMINAL_COMMON UINT32_C(0x8c013602)
#define TERMINAL_TABLE UINT32_C(0x8c013800)
static unsigned terminal_cases;
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
int main(void) {
    allowed_routes_and_scoped_storage();live_capability_mutations();
    other_identity_and_unknown_routes();cold_shape_refusals();
    printf("Terminal dispatch worker: %u checks, %u route/mutation cases; raw unknown failures, exact live fatal hook, full free-list and scoped storage preserved\n",checks,terminal_cases);
    return 0;
}
