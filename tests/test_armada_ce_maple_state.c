/* SPDX-License-Identifier: GPL-3.0-only */
#define main kui_armada_ce_maple_state_fixture_main
#include "test_armada_ce_maple_proof.c"
#undef main
#include "kui/armada_ce_maple_state.h"
static struct kui_armada_ce_maple_state_workspace state_workspace;
static struct kui_armada_ce_maple_state_snapshot state;
static const uint32_t ext_va=0x08030a60u,ext_p1=0x8c350a60u,ext_block=0x8cf04000u;
static const uint32_t tx_block=0x8cf05000u,rx_block=0x8cf06000u;
static const uint32_t tx_bus=0x0cfd3000u,rx_bus=0x0cfd4000u;
static const uint32_t fields[10]={0,0x84,0x88,0x8c,0x90,0x100,0xc0,0xc4,0xcc,0xd8};
static void reset_reads(void) {
    f.calls=f.fail_call=f.mutate_address=f.mutate_visit=f.mutate_value=0;
    for(unsigned i=0;i<f.cells;i++) f.cell[i].visits=0;
}
static void extension_word(unsigned index,uint32_t v) {assert(index<10u);put(ext_p1+fields[index],v);}
static bool read_state_ram(void *context,uint32_t address,uint32_t *out) {
    if(!find(address)) fprintf(stderr,"Unexpected state RAM read %08x after %u reads\n",address,f.calls);
    return read_ram(context,address,out);
}
static void prepare(uint32_t raw) {
    reset();put(ttb,owner_table);put(f.cached[8].p1,raw);put(f.cached[9].p1,0);
    assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_OK && proof.root[2]==raw && !proof.root[4]);
    put(owner_table+12u,ext_block);put(ext_block+12u,0x0c350079u);
    put(owner_table+20u,tx_block);put(tx_block+12u,0x0cfd3071u);
    put(owner_table+24u,rx_block);put(rx_block+12u,0x0cfd4071u);put(rx_block+16u,0x0cfd5071u);
    extension_word(0,0);extension_word(1,tx_bus);extension_word(2,0x00050000u);
    extension_word(3,0x00050000u);extension_word(4,0x0005000cu);extension_word(5,0x1e400000u);
    extension_word(6,rx_bus);extension_word(7,0);extension_word(8,0x00060000u);extension_word(9,rx_bus-0x00060000u);
    memset(&state_workspace,0,sizeof(state_workspace));memset(&state,0,sizeof(state));reset_reads();
}
static enum kui_armada_ce_maple_proof_status run_state(void) {
    struct kui_armada_ce_maple_snapshot a=old_snapshot;
    struct kui_armada_ce_maple_workspace b=old_workspace;
    struct kui_armada_ce_maple_proof_snapshot c=proof;
    struct kui_armada_ce_maple_proof_workspace d=workspace;
    enum kui_armada_ce_maple_proof_status status=kui_armada_ce_maple_state(&old_snapshot,&old_workspace,&proof,&workspace,
        read_state_ram,read_cache,&f,&state_workspace,&state);
    assert(!memcmp(&a,&old_snapshot,sizeof(a)) && !memcmp(&b,&old_workspace,sizeof(b)));
    assert(!memcmp(&c,&proof,sizeof(c)) && !memcmp(&d,&workspace,sizeof(d)));
    assert(state.counts[0]==(uint32_t)status && state.counts[1]==f.calls && f.calls<=2048u);
    return status;
}
static void success(void) {
    prepare(0x00030a60u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_OK);
    assert(state.counts[1]==1029u && state.counts[2]==31u && state.counts[3]==0x30a60u && state.counts[4]==ext_va);
    assert(state.extension[0]==ext_p1 && state.extension[1]==0x0c350079u && !state.extension[2]);
    assert(state.extension[3]==0x1e400000u && state.extension[4]==0x198u);
    assert(state.tx[0]==tx_bus && state.tx[1]==0x50000u && state.tx[2]==0x50000u && state.tx[3]==0x5000cu && state.tx[4]==0x8cfd3000u);
    assert(state.rx[0]==rx_bus && !state.rx[1] && state.rx[2]==0x60000u && state.rx[3]+state.rx[2]==rx_bus && state.rx[4]==0x8cfd4000u);
    assert(state.maps[0]==0x0cfd3071u && state.maps[1]==0x0cfd4071u && state.maps[2]==0x0cfd5071u);
    assert(state.maps[3]==owner_table && state.maps[4]==owner_table);
    /* There are deliberately no cells for TX/RX contents or MMIO VA. */
    assert(!find(0x8cfd3000u) && !find(0x8cfd4000u) && !find(0x8cfd5000u) && !find(0x1e400000u));
    prepare(ext_va);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_OK && state.counts[4]==ext_va);
    prepare(0x30a60u);extension_word(3,0);extension_word(4,0);
    assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_OK);
    prepare(0x30a60u);extension_word(2,0x08050000u);extension_word(3,0x08050000u);extension_word(4,0x08051000u);
    extension_word(8,0x08060000u);extension_word(9,rx_bus-0x08060000u);extension_word(5,0xa05f6c88u);
    assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_OK && state.extension[3]==0xa05f6c88u && !find(0xa05f6c88u));
    prepare(0x30a60u);f.hit_code=1u;assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_OK);
}
static void invalid_pointers_and_layout(void) {
    static const uint32_t raw[]={0,1u,0x10000u,0x01fffff0u,0x0a030a60u,0x8c350a60u,0xac350a60u,0xa05f6c88u};
    for(unsigned i=0;i<sizeof(raw)/sizeof(*raw);i++) {
        prepare(raw[i]);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_PROCESS);
        assert(state.counts[3]==raw[i] && !state.counts[4] && !visits(ext_p1|0x20000000u));
    }
    prepare(0x30a60u);put(ttb,table);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_PROCESS);
    assert(state.maps[3]==table && state.maps[4]==owner_table);
    static const struct {unsigned field;uint32_t v;enum kui_armada_ce_maple_proof_status status;} bad[]={
        {0,1,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},
        {1,0,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING},{1,0x8cfd3000u,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING},
        {1,0x0cfd3004u,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING},
        {2,0,KUI_ARMADA_CE_MAPLE_PROOF_PROCESS},{2,0x00050004u,KUI_ARMADA_CE_MAPLE_PROOF_PROCESS},
        {2,0x0a050000u,KUI_ARMADA_CE_MAPLE_PROOF_PROCESS},
        {3,0,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},{3,0x50004u,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},
        {3,0x50ffcu,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},{4,0x50008u,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},
        {4,0x51004u,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},{4,0,KUI_ARMADA_CE_MAPLE_PROOF_METADATA},
        {6,0x0cfd4004u,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING},{7,1,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING},
        {8,0x01fff000u,KUI_ARMADA_CE_MAPLE_PROOF_PROCESS},{8,0x60004u,KUI_ARMADA_CE_MAPLE_PROOF_PROCESS},
        {9,0,KUI_ARMADA_CE_MAPLE_PROOF_MAPPING}
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        prepare(0x30a60u);extension_word(bad[i].field,bad[i].v);assert(run_state()==bad[i].status);
        assert((state.counts[2]&2u) && state.extension[3]==0x1e400000u);
    }
    prepare(0x30a60u);put(tx_block+12u,0x0cfd3079u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_MAPPING && state.maps[0]==0x0cfd3079u);
    prepare(0x30a60u);put(tx_block+12u,0x0cfd6071u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_MAPPING && state.tx[4]==0x8cfd6000u);
    prepare(0x30a60u);put(rx_block+16u,0x0cfd6071u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_MAPPING && state.maps[2]==0x0cfd6071u);
    prepare(0x30a60u);put(rx_block+12u,0x0cfd4079u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_MAPPING);
    prepare(0x30a60u);put(ext_block+12u,0);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_MAPPING);
    prepare(0x30a60u);extension_word(8,0x50000u);extension_word(9,rx_bus-0x50000u);
    assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
    prepare(0x30a60u);extension_word(6,tx_bus);extension_word(9,tx_bus-0x60000u);
    assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
    prepare(0x30a60u);proof.counts[0]=KUI_ARMADA_CE_MAPLE_PROOF_CHANGED;
    assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA && !f.calls);
}
static void extension_buffer_aliases(void) {
    prepare(0x30a60u);extension_word(2,0x30000u);extension_word(3,0x30000u);extension_word(4,0x3000cu);
    assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
    assert(state.fault[1]==ext_va && state.fault[3]==0x08030000u && !(state.counts[2]&4u));
    prepare(0x30a60u);extension_word(8,0x30000u);extension_word(9,rx_bus-0x30000u);
    assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
    assert(state.fault[1]==ext_va && state.fault[3]==0x08030000u && !state.maps[1]);
    for(unsigned receive=0;receive<2u;receive++) {
        prepare(0x30a60u);
        uint32_t bus=receive?rx_bus:tx_bus,aliased_p1=(bus|0x80000000u)+0xa60u;
        put(ext_block+12u,bus|0x79u);
        for(unsigned i=0;i<10u;i++) put(aliased_p1+fields[i],value(ext_p1+fields[i]));
        assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
        assert(state.extension[0]==aliased_p1 && state.fault[2]==(bus+0xa60u) && state.fault[3]==bus);
        /* The extension-field reads necessarily preceded discovering the
         * corrupt allocation alias. No RX mapping or repeat follows refusal. */
        assert(visits(aliased_p1|0x20000000u)==2u && !visits(rx_block+12u));
        assert(!state.maps[1] && !state.maps[2]);
    }
}
static void failures_and_mutations(void) {
    prepare(0x30a60u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_OK);uint32_t total=f.calls;
    for(unsigned i=1;i<=total;i++) {
        prepare(0x30a60u);f.fail_call=i;assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_READ && f.calls==i);
    }
    for(unsigned i=0;i<10u;i++) {
        prepare(0x30a60u);uint32_t at=(ext_p1+fields[i])|0x20000000u;
        mutate(at,3u,value(ext_p1+fields[i])^4u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    }
    static const uint32_t changed[]={KUI_ARMADA_CE_MAPLE_TTB,KUI_ARMADA_CE_MAPLE_PTEH,KUI_ARMADA_CE_MAPLE_CCR,
        KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS,KUI_ARMADA_CE_MAPLE_HEAP_LOW,KUI_ARMADA_CE_MAPLE_HEAP_HIGH,
        KUI_ARMADA_CE_MAPLE_MODULE_HEAD,process,process+12u,process+8u,module+20u,head+96u,module+12u};
    for(unsigned i=0;i<sizeof(changed)/sizeof(*changed);i++) {
        prepare(0x30a60u);mutate(changed[i],2u,value(changed[i])^4u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    }
    prepare(0x30a60u);mutate(ttb,2u,table);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    prepare(0x30a60u);mutate(KUI_ARMADA_CE_MAPLE_CCR,3u,0x125u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    prepare(0x30a60u);mutate(block+0x20u,2u,0x0c360079u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    prepare(0x30a60u);mutate(ext_block+12u,2u,0x0c360079u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    prepare(0x30a60u);mutate(tx_block+12u,3u,0x0cfd6071u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    prepare(0x30a60u);mutate(rx_block+16u,3u,0x0cfd6071u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    prepare(0x30a60u);mutate(rx_block+12u,3u,0x0cfd6071u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    prepare(0x30a60u);mutate(f.cached[8].p1|0x20000000u,3u,0x30a64u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    prepare(0x30a60u);f.dirty_alias=1;assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CACHE);
    prepare(0x30a60u);mutate(0xf4000a60u,2u,0x0c350001u);assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    printf("CE Maple state callback failure positions checked: %u.\n",total);
}
static void largest_and_arguments(void) {
    prepare(0x30a60u);
    for(unsigned i=0;i<49u;i++) node(head+i*0x100u,i==48u?module:head+(i+1u)*0x100u,false);
    memset(&old_snapshot,0,sizeof(old_snapshot));memset(&old_workspace,0,sizeof(old_workspace));reset_reads();
    assert(kui_armada_ce_maple_observe(&old_workspace,&old_snapshot,read_ram,&f)==KUI_ARMADA_CE_MAPLE_MISSING);
    assert(old_workspace.nodes==50u);reset_reads();assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_OK);
    reset_reads();assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_OK && f.calls==1709u);
    prepare(0x30a60u);
    assert(kui_armada_ce_maple_state(NULL,&old_workspace,&proof,&workspace,read_ram,read_cache,&f,&state_workspace,&state)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(kui_armada_ce_maple_state(&old_snapshot,&old_workspace,&proof,&workspace,read_ram,NULL,&f,&state_workspace,&state)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(kui_armada_ce_maple_state(&old_snapshot,&old_workspace,&proof,&workspace,read_ram,read_cache,&f,&state_workspace,(struct kui_armada_ce_maple_state_snapshot *)(void *)&proof)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(kui_armada_ce_maple_state(&old_snapshot,&old_workspace,&proof,&workspace,read_ram,read_cache,&f,
        (struct kui_armada_ce_maple_state_workspace *)(void *)&old_workspace,&state)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(!f.calls);
}
#ifndef KUI_ARMADA_CE_MAPLE_STATE_TEST_MAIN
#define KUI_ARMADA_CE_MAPLE_STATE_TEST_MAIN main
#endif
int KUI_ARMADA_CE_MAPLE_STATE_TEST_MAIN(void) {
    success();invalid_pointers_and_layout();extension_buffer_aliases();failures_and_mutations();largest_and_arguments();
    puts("Passive CE Maple state: low/explicit owner pointers, prior witnesses, ten metadata words, map-only uncached buffers, raw refusals and immutable inputs passed.");
    return 0;
}
