/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the ordinary RAM/module/owner fixture. Its guard words are authored
 * CRC-correct synthetic data, never bytes copied from a private CE image. */
#define main kui_armada_ce_live_sound_fixture_main
#include "test_armada_ce_live_sound.c"
#undef main

static void stream_fixture(struct test_memory *m,unsigned owner) {
    fixture(m,owner);put(m,owner_process(owner)+8u,0x12345679u);
    static const uint32_t rva[11]={0x8d20u,0x8814u,0x8708u,0x8834u,0x877cu,0x852cu,
        0x88acu,0x88b4u,0x8c44u,0x8cd8u,0x890cu};
    static const uint32_t size[11]={8u,8u,32u,8u,32u,8u,8u,8u,20u,8u,8u};
    static const uint32_t crc[11]={0xc1eabb03u,0xd3f7581fu,0xa6a3bf77u,0xe1c13a9du,
        0xddf88da0u,0x4dcea5a7u,0x3a94fd2au,0x08a29fa8u,0x7ae32575u,0x914c763fu,0xdce3a077u};
    for(unsigned i=0;i<11u;i++) crc_guard(m,TEST_CORE_BASE+rva[i],size[i],crc[i]);
    virtual_put(m,ds_alias(owner,0x11ccu)+52u,TEST_DS_BASE+0x3154u);
    crc_guard(m,TEST_DS_BASE+0x3154u,32u,0x0d46861au);
}
static enum kui_armada_ce_live_status stream_bind(struct test_memory *m,
    struct kui_armada_ce_live_sound *sound,struct kui_armada_ce_live_stream_api *api) {
    enum kui_armada_ce_live_status status=kui_armada_ce_live_stream_api_bind(&bind_work,sound,api);
    if(status!=KUI_ARMADA_CE_LIVE_OK) {
        struct kui_armada_ce_live_stream_api empty={0};
        assert(!memcmp(api,&empty,sizeof(empty)));
    }
    assert(bind_work.reads==m->reads && m->reads<=TEST_READS);return status;
}
int main(void) {
    static struct test_memory m;
    struct kui_armada_ce_live_sound sound;
    struct kui_armada_ce_live_stream_api api;
    unsigned cases=0;
    stream_fixture(&m,5u);assert(bind(&m,&sound)==KUI_ARMADA_CE_LIVE_OK);
    unsigned start=m.reads;
    assert(stream_bind(&m,&sound,&api)==KUI_ARMADA_CE_LIVE_OK);++cases;
    unsigned extra=m.reads-start;
    assert(api.owner_process==owner_process(5u) && api.owner_slot==owner_slot(5u) &&
        api.owner_handle==0x12345679u && api.coredll_module==TEST_CORE_NODE &&
        api.create_thread==TEST_CORE_BASE+0x8d20u && api.sleep==TEST_CORE_BASE+0x8708u &&
        api.set_thread_priority==TEST_CORE_BASE+0x8834u && api.set_kmode==TEST_CORE_BASE+0x8c44u &&
        api.buffer_set_position==TEST_DS_BASE+0x3154u);++cases;
    /* Corrupt every callable prefix before the extension: no API target leaks. */
    static const uint32_t guard[12]={0x8d20u,0x8814u,0x8708u,0x8834u,0x877cu,0x852cu,
        0x88acu,0x88b4u,0x8c44u,0x8cd8u,0x890cu,0x3154u};
    for(unsigned i=0;i<12u;i++) {
        stream_fixture(&m,5u);assert(bind(&m,&sound)==KUI_ARMADA_CE_LIVE_OK);
        uint32_t va=(i==11u?TEST_DS_BASE:TEST_CORE_BASE)+guard[i];
        virtual_put(&m,va,virtual_get(&m,va)^4u);
        assert(stream_bind(&m,&sound,&api)==KUI_ARMADA_CE_LIVE_IDENTITY);++cases;
    }
    /* A changed executable prefix during the repeat transaction fails closed. */
    stream_fixture(&m,5u);assert(bind(&m,&sound)==KUI_ARMADA_CE_LIVE_OK);
    m.change_address=physical(&m,TEST_CORE_BASE+0x8d20u);m.change_on=2u;m.change_mask=4u;
    assert(stream_bind(&m,&sound,&api)==KUI_ARMADA_CE_LIVE_CHANGED);++cases;
    for(unsigned failure=1u;failure<=extra;failure++) {
        stream_fixture(&m,5u);assert(bind(&m,&sound)==KUI_ARMADA_CE_LIVE_OK);
        m.fail_at=m.reads+failure;
        assert(stream_bind(&m,&sound,&api)==KUI_ARMADA_CE_LIVE_READ);++cases;
    }
    stream_fixture(&m,5u);assert(bind(&m,&sound)==KUI_ARMADA_CE_LIVE_OK);
    put(&m,owner_process(5u)+8u,0u);
    assert(stream_bind(&m,&sound,&api)==KUI_ARMADA_CE_LIVE_CHANGED);++cases;
    stream_fixture(&m,5u);assert(bind(&m,&sound)==KUI_ARMADA_CE_LIVE_OK);
    bind_work.reads=TEST_READS;
    memset(&api,0xaa,sizeof(api));
    assert(kui_armada_ce_live_stream_api_bind(&bind_work,&sound,&api)==KUI_ARMADA_CE_LIVE_BUDGET);++cases;
    struct kui_armada_ce_live_stream_api zero={0};assert(!memcmp(&api,&zero,sizeof(zero)));
    assert(kui_armada_ce_live_stream_api_bind(NULL,&sound,&api)==KUI_ARMADA_CE_LIVE_ARGUMENT);++cases;
    printf("armada CE native API: %u cases, %u extension reads\n",cases,extra);
    return 0;
}
