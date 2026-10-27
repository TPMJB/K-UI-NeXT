/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kui/armada_ce_maple_probe.h"
#include "kui/gd_service.h"
#include "kui/retail_gd.h"

/* Closed read-only fixture: only individually supplied words exist. An
 * attempted DMA-chain, section-table or VMU-data read fails the test. */
enum { CELL_CAPACITY=2048,LOG_CAPACITY=1200 };
struct cell {uint32_t address,value,visits;};
struct fixture {
    struct cell cell[CELL_CAPACITY];
    uint32_t log[LOG_CAPACITY],cells,calls,fail_call;
    uint32_t mutate_address,mutate_visit,mutate_value;
};
static struct fixture fixture;
static struct kui_armada_ce_maple_workspace workspace;
static struct kui_armada_ce_maple_snapshot snapshot;
static const uint32_t heap=UINT32_C(0x8c200000);
static const uint32_t maple=UINT32_C(0x8c200000),mapledev=UINT32_C(0x8c200100);
static const uint32_t maple_base=UINT32_C(0x03dd0000),mapledev_base=UINT32_C(0x05d20000);

static struct cell *cell_at(struct fixture *f,uint32_t address) {
    for(unsigned i=0;i<f->cells;i++) if(f->cell[i].address==address) return &f->cell[i];
    return NULL;
}
static void put(uint32_t address,uint32_t value) {
    struct cell *c=cell_at(&fixture,address);
    if(c) c->value=value;
    else {assert(fixture.cells<CELL_CAPACITY);fixture.cell[fixture.cells++]=(struct cell){address,value,0};}
}
static bool read_fixture(void *context,uint32_t address,uint32_t *value) {
    struct fixture *f=context;
    assert(f && value && f->calls<LOG_CAPACITY);
    assert(kui_armada_ce_maple_read_address(address));
    /* Both undocumented registers are excluded, as are all other MMIO. */
    assert(address!=UINT32_C(0xa05f6c88) && address!=UINT32_C(0xa05f6c8c));
    f->log[f->calls++]=address;
    if(f->fail_call && f->calls==f->fail_call) return false;
    struct cell *c=cell_at(f,address);
    assert(c); /* Unknown pointed-to bytes must never reach the callback. */
    ++c->visits;
    *value=(address==f->mutate_address && c->visits==f->mutate_visit)?f->mutate_value:c->value;
    return true;
}
bool kui_armada_ce_maple_probe_test_read(void *unused,uint32_t address,uint32_t *value) {
    assert(unused==NULL);
    return read_fixture(&fixture,address,value);
}
static uint32_t visits(uint32_t address) {
    struct cell *c=cell_at(&fixture,address);return c?c->visits:0;
}
static uint32_t value_at(uint32_t address) {
    struct cell *c=cell_at(&fixture,address);assert(c);return c->value;
}
static void node(uint32_t address,uint32_t next,uint32_t toc,uint32_t kind,
                 uint32_t base,uint32_t span) {
    put(address,address);put(address+4u,next);put(address+12u,1u);
    put(address+80u,base);put(address+84u,0x210eu);put(address+96u,toc);
    put(address+100u,kind);put(address+112u,3u);put(address+124u,span);
    put(address+176u,base+span-0x100u);
}
static void reset(void) {
    memset(&fixture,0,sizeof(fixture));memset(&workspace,0,sizeof(workspace));memset(&snapshot,0,sizeof(snapshot));
    put(KUI_ARMADA_CE_MAPLE_TTB,0x8c1458a0u);put(KUI_ARMADA_CE_MAPLE_PTEH,0x03dd0007u);
    put(KUI_ARMADA_CE_MAPLE_CCR,0x105u);put(KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS,KUI_ARMADA_CE_MAPLE_PROCESS_BASE);
    put(KUI_ARMADA_CE_MAPLE_HEAP_LOW,heap);put(KUI_ARMADA_CE_MAPLE_HEAP_HIGH,heap+0x10000u);
    put(KUI_ARMADA_CE_MAPLE_MODULE_HEAD,maple);
    node(maple,mapledev,KUI_ARMADA_CE_MAPLE_ROM_TOC,3u,maple_base,KUI_ARMADA_CE_MAPLE_SPAN);
    node(mapledev,0,KUI_ARMADA_CE_MAPLEDEV_ROM_TOC,3u,mapledev_base,KUI_ARMADA_CE_MAPLEDEV_SPAN);
    /* Deliberately unbacked DMA source. Its value must never be followed. */
    put(KUI_ARMADA_CE_MAPLE_MDSTAR,0x0cdead00u);put(KUI_ARMADA_CE_MAPLE_MDTSEL,1u);
    put(KUI_ARMADA_CE_MAPLE_MDEN,1u);put(KUI_ARMADA_CE_MAPLE_MDST,0u);put(KUI_ARMADA_CE_MAPLE_ISTNRM,0x1000u);
}
static enum kui_armada_ce_maple_status observe(void) {
    enum kui_armada_ce_maple_status status=kui_armada_ce_maple_observe(&workspace,&snapshot,read_fixture,&fixture);
    assert(snapshot.counts[3]==fixture.calls);
    assert(fixture.calls<=KUI_ARMADA_CE_MAPLE_READ_LIMIT);
    return status;
}
static void expect_refused(enum kui_armada_ce_maple_status expected) {
    assert(observe()==expected);
    assert(snapshot.counts[0]==1u && !snapshot.counts[1] && snapshot.counts[2]==1u);
    assert(snapshot.counts[4]==(uint32_t)expected && snapshot.fault[0]==(uint32_t)expected);
}
static void valid_and_missing(void) {
    reset();assert(observe()==KUI_ARMADA_CE_MAPLE_OK);
    assert(snapshot.counts[0]==1 && snapshot.counts[1]==1 && !snapshot.counts[2]);
    assert(snapshot.counts[3]==59u && workspace.nodes==2u);
    assert(snapshot.cpu[0]==0x8c1458a0u && snapshot.cpu[1]==0x03dd0007u && snapshot.cpu[2]==0x105u);
    assert(snapshot.cpu[3]==KUI_ARMADA_CE_MAPLE_PROCESS_BASE && snapshot.cpu[4]==maple);
    const uint32_t m[5]={maple,maple_base,KUI_ARMADA_CE_MAPLE_SPAN,3u,1u};
    const uint32_t d[5]={mapledev,mapledev_base,KUI_ARMADA_CE_MAPLEDEV_SPAN,3u,1u};
    assert(!memcmp(snapshot.maple,m,sizeof(m)) && !memcmp(snapshot.mapledev,d,sizeof(d)));
    const uint32_t regs[5]={0x0cdead00u,1u,1u,0u,0x1000u};
    assert(!memcmp(snapshot.registers,regs,sizeof(regs)));
    for(unsigned i=0;i<5u;i++) assert(!snapshot.fault[i]);
    assert(visits(KUI_ARMADA_CE_MAPLE_MDSTAR)==1u && visits(KUI_ARMADA_CE_MAPLE_MODULE_HEAD)==2u);

    reset();put(maple+96u,KUI_ARMADA_CE_MAPLE_ROM_TOC+4u);
    assert(observe()==KUI_ARMADA_CE_MAPLE_MISSING);
    assert(!snapshot.maple[0] && snapshot.mapledev[0]==mapledev && snapshot.counts[1]==1u);
    assert(snapshot.fault[0]==KUI_ARMADA_CE_MAPLE_MISSING && snapshot.fault[4]==2u);

    /* Kind2 +96 is a filesystem handle even if it equals a ROM TOC and all
     * other words look identical. It cannot establish either ROM identity. */
    reset();put(maple+100u,2u);put(mapledev+100u,2u);
    assert(observe()==KUI_ARMADA_CE_MAPLE_MISSING);
    assert(!snapshot.maple[0] && !snapshot.mapledev[0]);
    reset();put(KUI_ARMADA_CE_MAPLE_MODULE_HEAD,0u);
    assert(observe()==KUI_ARMADA_CE_MAPLE_MISSING && workspace.nodes==0u);
}
static void bad_context_and_heap(void) {
    static const uint32_t bad_root[]={0x8c000100u,0xac1458a0u,0xff000008u,0x8c1458a1u};
    for(unsigned i=0;i<sizeof(bad_root)/sizeof(*bad_root);i++) {
        reset();put(KUI_ARMADA_CE_MAPLE_TTB,bad_root[i]);expect_refused(KUI_ARMADA_CE_MAPLE_CONTEXT);
        assert(!visits(maple) && !visits(KUI_ARMADA_CE_MAPLE_MDSTAR));
    }
    reset();put(KUI_ARMADA_CE_MAPLE_CCR,0x125u);expect_refused(KUI_ARMADA_CE_MAPLE_CONTEXT);
    reset();put(KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS,KUI_ARMADA_CE_MAPLE_PROCESS_BASE+1u);expect_refused(KUI_ARMADA_CE_MAPLE_CONTEXT);
    reset();put(KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS,KUI_ARMADA_CE_MAPLE_PROCESS_BASE+32u*KUI_ARMADA_CE_MAPLE_PROCESS_STRIDE);expect_refused(KUI_ARMADA_CE_MAPLE_CONTEXT);
    static const uint32_t bad_low[]={0x8c000000u,0xac200000u,0x8c200001u,0xa05f6c00u,0x8c220000u};
    for(unsigned i=0;i<sizeof(bad_low)/sizeof(*bad_low);i++) {
        reset();put(KUI_ARMADA_CE_MAPLE_HEAP_LOW,bad_low[i]);expect_refused(KUI_ARMADA_CE_MAPLE_HEAP);
        assert(!visits(maple));
    }
    reset();put(KUI_ARMADA_CE_MAPLE_HEAP_HIGH,heap);expect_refused(KUI_ARMADA_CE_MAPLE_HEAP);
    reset();put(KUI_ARMADA_CE_MAPLE_HEAP_HIGH,0x8d000004u);expect_refused(KUI_ARMADA_CE_MAPLE_HEAP);
    reset();put(KUI_ARMADA_CE_MAPLE_HEAP_HIGH,heap+0x10001u);expect_refused(KUI_ARMADA_CE_MAPLE_HEAP);
}
static void bad_nodes_and_modules(void) {
    static const uint32_t bad_node[]={0x8c000100u,0xac200000u,0xa05f6c88u,0x8c200001u,0x8c210000u,0x8c20ff80u};
    for(unsigned i=0;i<sizeof(bad_node)/sizeof(*bad_node);i++) {
        reset();put(KUI_ARMADA_CE_MAPLE_MODULE_HEAD,bad_node[i]);expect_refused(KUI_ARMADA_CE_MAPLE_NODE);
        assert(fixture.calls==12u); /* Fixed MMIO sampled; rejected node never read. */
    }
    reset();put(maple+4u,maple);expect_refused(KUI_ARMADA_CE_MAPLE_CYCLE);
    reset();put(mapledev+4u,maple);expect_refused(KUI_ARMADA_CE_MAPLE_CYCLE);
    reset();put(maple,maple+4u);expect_refused(KUI_ARMADA_CE_MAPLE_NODE);
    assert(snapshot.fault[1]==maple && snapshot.fault[2]==maple+4u && snapshot.fault[3]==maple);
    reset();put(mapledev+4u,0xa05f6c88u);expect_refused(KUI_ARMADA_CE_MAPLE_NODE);
    reset();put(mapledev+96u,KUI_ARMADA_CE_MAPLE_ROM_TOC);expect_refused(KUI_ARMADA_CE_MAPLE_MODULE);
    /* Exact TOC with bad runtime geometry is a refused candidate, never a
     * silently admitted external module or an owner/callable address. */
    const uint32_t field[]={80u,80u,80u,124u,112u,12u,176u,176u};
    const uint32_t bad[]={KUI_ARMADA_CE_MAPLE_NOMINAL,0xa3dd0000u,0x03dc0000u,0x9000u,4u,0u,maple_base+1u,maple_base+0xa000u};
    for(unsigned i=0;i<sizeof(field)/sizeof(*field);i++) {
        reset();put(maple+field[i],bad[i]);expect_refused(KUI_ARMADA_CE_MAPLE_MODULE);
        assert(snapshot.maple[0]==maple && snapshot.fault[1]==maple+field[i]);
        assert(snapshot.registers[0]==0x0cdead00u && snapshot.registers[1]==1u);
    }
}
static void failure_and_changes(void) {
    /* Every callback position in an otherwise valid complete sample. */
    for(unsigned i=1;i<=59u;i++) {
        reset();fixture.fail_call=i;expect_refused(KUI_ARMADA_CE_MAPLE_READ);
        assert(snapshot.counts[3]==i && snapshot.fault[1]==fixture.log[i-1u]);
    }
    static const uint32_t globals[]={KUI_ARMADA_CE_MAPLE_TTB,KUI_ARMADA_CE_MAPLE_PTEH,KUI_ARMADA_CE_MAPLE_CCR,
        KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS,KUI_ARMADA_CE_MAPLE_HEAP_LOW,KUI_ARMADA_CE_MAPLE_HEAP_HIGH,KUI_ARMADA_CE_MAPLE_MODULE_HEAD};
    static const uint32_t offsets[]={0u,4u,12u,80u,84u,96u,100u,112u,124u,176u};
    for(unsigned i=0;i<sizeof(globals)/sizeof(*globals);i++) {
        reset();fixture.mutate_address=globals[i];fixture.mutate_visit=2u;fixture.mutate_value=value_at(globals[i])^4u;
        expect_refused(KUI_ARMADA_CE_MAPLE_CHANGED);assert(snapshot.fault[1]==globals[i]);
        assert(snapshot.fault[2]==fixture.mutate_value && snapshot.fault[3]==value_at(globals[i]));
    }
    for(unsigned n=0;n<2u;n++) for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);i++) {
        reset();uint32_t address=(n?mapledev:maple)+offsets[i];
        fixture.mutate_address=address;fixture.mutate_visit=2u;fixture.mutate_value=value_at(address)^4u;
        expect_refused(KUI_ARMADA_CE_MAPLE_CHANGED);assert(snapshot.fault[1]==address);
    }
    /* Unrelated list entries receive the same complete stability checks. */
    reset();put(maple+4u,heap+0x200u);
    node(heap+0x200u,mapledev,0x12345678u,2u,0x10000u,0x2000u);
    fixture.mutate_address=heap+0x200u+96u;fixture.mutate_visit=2u;fixture.mutate_value=9u;
    expect_refused(KUI_ARMADA_CE_MAPLE_CHANGED);
}
static void limits_and_arguments(void) {
    static const uint32_t counts[]={50u,51u,64u,65u};
    for(unsigned case_index=0;case_index<sizeof(counts)/sizeof(*counts);case_index++) {
        unsigned count=counts[case_index];
        reset();
        for(unsigned i=2u;i<count;i++) node(heap+i*0x100u,i+1u<count?heap+(i+1u)*0x100u:0u,0x12345678u,2u,0u,0u);
        put(mapledev+4u,heap+0x200u);
        if(count==50u) {
            assert(observe()==KUI_ARMADA_CE_MAPLE_OK && fixture.calls==1019u);
            assert(snapshot.counts[1]==1u && !snapshot.counts[2]);
        } else {
            expect_refused(count==65u?KUI_ARMADA_CE_MAPLE_LIMIT:KUI_ARMADA_CE_MAPLE_BUDGET);
            if(count!=65u) assert(fixture.calls==KUI_ARMADA_CE_MAPLE_READ_LIMIT);
        }
        assert(workspace.nodes==(count==65u?64u:count));
    }
    reset();assert(kui_armada_ce_maple_observe(NULL,&snapshot,read_fixture,&fixture)==KUI_ARMADA_CE_MAPLE_ARGUMENT);
    assert(kui_armada_ce_maple_observe(&workspace,NULL,read_fixture,&fixture)==KUI_ARMADA_CE_MAPLE_ARGUMENT);
    assert(kui_armada_ce_maple_observe(&workspace,&snapshot,NULL,&fixture)==KUI_ARMADA_CE_MAPLE_ARGUMENT);
    assert(kui_armada_ce_maple_observe(&workspace,(struct kui_armada_ce_maple_snapshot *)(void *)&workspace,read_fixture,&fixture)==KUI_ARMADA_CE_MAPLE_ARGUMENT);
    assert(!fixture.calls && !snapshot.counts[0]);
    uint32_t word=0;
    assert(kui_armada_ce_maple_read(&workspace,&snapshot,read_fixture,&fixture,0xa05f6c88u,&word)==KUI_ARMADA_CE_MAPLE_NODE);
    assert(!fixture.calls);
}
static void dormant_and_capped_entry(void) {
    reset();kui_armada_ce_maple_probe_test_reset();
    const struct kui_armada_ce_maple_snapshot *s=kui_armada_ce_bind_probe(0u,0u,0,0u);
    struct kui_armada_ce_maple_snapshot before=*s;
    assert(!s->counts[0] && s->counts[4]==KUI_ARMADA_CE_MAPLE_DORMANT);
    assert(kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,0u)==s);
    assert(kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,0,1u)==s);
    assert(kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,-1,1u)==s);
    assert(kui_armada_ce_bind_probe(KUI_GD_EXEC,KUI_RETAIL_GD_PLAY,1,1u)==s);
    assert(kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_SEEK,1,1u)==s);
    assert(kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,2u)==s);
    assert(!fixture.calls && !memcmp(s,&before,sizeof(before)));
    for(unsigned i=0;i<KUI_ARMADA_CE_MAPLE_SAMPLE_LIMIT;i++) {
        /* The production snapshot persists; the closed memory model is fresh
         * for every call and no call inherits an artificial callback counter. */
        reset();s=kui_armada_ce_bind_probe(KUI_GD_REQUEST,i&1u?KUI_RETAIL_GD_PLAY:KUI_RETAIL_GD_PLAY2,1,1u);
        assert(s->counts[0]==i+1u && s->counts[1]==i+1u && !s->counts[2]);
        assert(s->counts[3]==59u && s->counts[4]==KUI_ARMADA_CE_MAPLE_OK && fixture.calls==59u);
    }
    before=*s;reset();fixture.fail_call=1u;
    assert(kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,1u)==s);
    assert(kui_armada_ce_bind_probe(UINT32_MAX,0u,0,0u)==s);
    assert(!fixture.calls && !memcmp(s,&before,sizeof(before)));
    assert(kui_armada_ce_maple_probe_contract==KUI_ARMADA_CE_MAPLE_PROBE_CONTRACT);
}
int main(void) {
    valid_and_missing();bad_context_and_heap();bad_nodes_and_modules();failure_and_changes();
    limits_and_arguments();dormant_and_capped_entry();
    puts("Passive Armada CE Maple: exact ROM metadata, external distinction, bounded reads, full witnesses, refusal and dormant/capped entry passed.");
    return 0;
}
