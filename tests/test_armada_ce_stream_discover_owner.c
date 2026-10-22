/* SPDX-License-Identifier: GPL-3.0-only */
/* Authored kernel metadata and synthetic backing; no private image bytes. */
#define main kui_armada_ce_live_sound_fixture_main
#include "test_armada_ce_live_sound.c"
#undef main
#include "kui/armada_ce_stream_initial_word.h"

static struct test_memory discovery_memory;
static struct kui_armada_ce_live_workspace discovery_work;
static bool discovery_table(void *context,uint32_t address,uint32_t *value) {
    bool allowed=address==KUI_ARMADA_CE_LIVE_CURRENT_PROCESS ||
        address==KUI_ARMADA_CE_LIVE_MODULE_HEAD || address==0x8c14d430u || address==0x8c14d434u;
    if(address>=KUI_ARMADA_CE_LIVE_PROCESS_BASE &&
       address<KUI_ARMADA_CE_LIVE_PROCESS_BASE+32u*KUI_ARMADA_CE_LIVE_PROCESS_STRIDE) {
        uint32_t offset=(address-KUI_ARMADA_CE_LIVE_PROCESS_BASE)%KUI_ARMADA_CE_LIVE_PROCESS_STRIDE;
        allowed=offset==0u || offset==8u || offset==12u;
    }
    if(address>=0x8c300100u && address<0x8c308000u) {
        uint32_t offset=address&255u;
        allowed=offset==0u || offset==4u || offset==12u ||
            (offset>=16u && offset<=76u && !(offset&3u)) || offset==80u || offset==84u ||
            offset==96u || offset==100u || offset==112u || offset==124u || offset==176u;
    }
    /* The complete ledger excludes page tables, virtual words, physical DLL
     * backing, cache arrays, CPU/MMIO and all sound-memory addresses. */
    assert(allowed);
    return read_word(context,address,value);
}
static void discovery_fixture(bool disk) {
    struct test_memory *m=&discovery_memory;
    fixture(m,5u);
    if(disk) {disk_sound(m,true,true);disk_sound(m,false,true);}
    const uint32_t base=0x03cc0000u,table=base+0x6198u;
    put(m,TEST_SN_NODE+80u,base);put(m,TEST_SN_NODE+176u,table);
    put(m,KUI_ARMADA_CE_LIVE_MODULE_HEAD,TEST_SN_NODE);
    put(m,TEST_SN_NODE+4u,TEST_DS_NODE);put(m,TEST_DS_NODE+4u,TEST_CORE_NODE);
    put(m,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,owner_process(3u));
    put(m,owner_process(3u)+12u,owner_slot(3u));put(m,owner_process(3u)+8u,0x12345673u);
    put(m,owner_process(5u)+8u,0x12345675u);
    /* No words exist at the relocated SNDCORE table or executable prefixes.
     * Discovery must succeed without ever reading that virtual image. */
}
static enum kui_armada_ce_live_status discover(struct kui_armada_ce_stream_initial_owner *out) {
    memset(out,0xa5,sizeof(*out));
    enum kui_armada_ce_live_status status=kui_armada_ce_stream_discover_owner(TEST_TTB,
        TEST_CCR,discovery_table,&discovery_memory,&discovery_work,out);
    assert(discovery_work.reads==discovery_memory.reads &&
        discovery_work.reads<=KUI_ARMADA_CE_LIVE_READ_LIMIT);
    assert(!discovery_work.pages && !discovery_work.mapped32 &&
        !discovery_work.native_values && !discovery_work.native_candidate &&
        !discovery_memory.cache_reads && !discovery_memory.cache_calls);
    if(status!=KUI_ARMADA_CE_LIVE_OK) {
        const struct kui_armada_ce_stream_initial_owner zero={0};
        assert(!memcmp(out,&zero,sizeof(zero)));
    }
    return status;
}
int main(void) {
    struct kui_armada_ce_stream_initial_owner out;
    struct test_memory *m=&discovery_memory;
    unsigned checks=0,successful_reads=0;
    discovery_fixture(true);
    assert(discover(&out)==KUI_ARMADA_CE_LIVE_OK);
    assert(out.process==owner_process(5u) && out.slot==owner_slot(5u) &&
        out.handle==0x12345675u && out.index==5u &&
        get(m,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS)==owner_process(3u));
    assert(discovery_work.sndcore.base==0x03cc0000u &&
        discovery_work.sndcore.sections==0x03cc6198u);
    const struct kui_armada_ce_stream_initial_owner driver={owner_process(3u),
        owner_slot(3u),0x12345673u,3u};
    assert(!kui_armada_ce_stream_initial_address(&driver,&discovery_work,0x03cc6198u));
    assert(kui_armada_ce_stream_initial_address(&out,&discovery_work,0x03cc6198u));
    successful_reads=m->reads;++checks;
    discovery_fixture(false);assert(discover(&out)==KUI_ARMADA_CE_LIVE_OK);++checks;
    /* Every selected module needs a real reference in the callback owner. */
    discovery_fixture(true);put(m,TEST_SN_NODE+24u,0u);
    assert(discover(&out)==KUI_ARMADA_CE_LIVE_OWNER);++checks;
    discovery_fixture(true);m->change_address=TEST_SN_NODE+24u;
    m->change_on=2u;m->change_mask=0x00010000u;
    assert(discover(&out)==KUI_ARMADA_CE_LIVE_CHANGED);++checks;
    discovery_fixture(true);m->change_address=owner_process(5u)+8u;
    m->change_on=2u;m->change_mask=4u;
    assert(discover(&out)==KUI_ARMADA_CE_LIVE_CHANGED &&
        discovery_work.fault[1]==owner_process(5u)+8u);++checks;
    /* Metadata-only routing never guesses between two attached processes. */
    discovery_fixture(true);
    const uint32_t nodes[3]={TEST_DS_NODE,TEST_SN_NODE,TEST_CORE_NODE};
    for(unsigned i=0;i<3u;i++) {
        put(m,nodes[i]+12u,(UINT32_C(1)<<4)|(UINT32_C(1)<<5));
        put(m,nodes[i]+24u,0x00020002u);
    }
    put(m,owner_process(4u)+12u,owner_slot(4u));put(m,owner_process(4u)+8u,0x12345674u);
    assert(discover(&out)==KUI_ARMADA_CE_LIVE_OWNER);++checks;
    discovery_fixture(true);m->change_address=TEST_SN_NODE+4u;
    m->change_on=2u;m->change_mask=0x100u;
    assert(discover(&out)==KUI_ARMADA_CE_LIVE_CHANGED);++checks;
    discovery_fixture(true);m->change_address=TEST_SN_NODE+176u;
    m->change_on=2u;m->change_mask=4u;
    assert(discover(&out)==KUI_ARMADA_CE_LIVE_CHANGED);++checks;
    /* Even the rejected external-shape probe words must stay unchanged. */
    discovery_fixture(true);unrelated_disk_nodes(m,1u);
    put(m,0x8c300404u,TEST_SN_NODE);m->change_address=0x8c30047cu;
    m->change_on=2u;m->change_mask=0x1000u;
    assert(discover(&out)==KUI_ARMADA_CE_LIVE_CHANGED);++checks;
    discovery_fixture(true);m->fail_at=4u;
    assert(discover(&out)==KUI_ARMADA_CE_LIVE_READ);++checks;
    discovery_fixture(true);put(m,owner_process(5u)+12u,owner_slot(4u));
    assert(discover(&out)==KUI_ARMADA_CE_LIVE_OWNER);++checks;
    printf("Armada callback-owner discovery: %u focused checks, %u bounded metadata reads; CURRENT=8c14e8b4 routes to OWNER=8c14e9ec despite SNDCORE table03cc6198, zero virtual/cache/native/API reads.\n",
        checks,successful_reads);
    return 0;
}
