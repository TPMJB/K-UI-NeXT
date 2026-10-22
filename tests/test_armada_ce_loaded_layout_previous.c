/* SPDX-License-Identifier: GPL-3.0-only */
/* Compile with the pinned b9aa5c70a431 header first on the include path and
 * -DKUI_ARMADA_PREVIOUS_LOADED_LAYOUT to replay the previous refusal. Compile
 * normally to require current admission of the identical authored memory.
 * This reproduces a possible 204-read path, not the unseen console list. */
#define main kui_armada_live_sound_stock_main
#include "test_armada_ce_live_sound.c"
#undef main

/* A read-only simulation of an admitted coherent mapped-word boundary. The
 * regular budgeted page-table walk still performs its six reads per word.
 * Cache-array behavior itself is independently exercised by the stock live
 * sound and cache-word tests; this fixture isolates loaded metadata changes. */
static enum kui_armada_ce_ram_bind_status loaded_layout_word(
    void *context,uint32_t va,uint32_t p1,uint32_t page,uint32_t ccr,uint32_t *out) {
    (void)va;(void)page;(void)ccr;
    uint32_t canonical;
    assert(kui_armada_ce_ram_table_p1(p1,&canonical) && p1==canonical);
    return lookup(context,p1,out)?KUI_ARMADA_CE_RAM_BIND_OK:KUI_ARMADA_CE_RAM_BIND_READ;
}
int main(void) {
    static struct test_memory m,before;
    struct kui_armada_ce_live_sound out;
    fixture(&m,2u);disk_sound(&m,true,true);disk_sound(&m,false,true);
    unrelated_disk_nodes(&m,13u);
    /* Sixteen total modules: eleven disk nodes and five ROM nodes. */
    for(unsigned i=0;i<4u;i++) put(&m,0x8c300400u+0x100u*i+100u,3u);
    for(unsigned profile=0;profile<2u;profile++) {
        const uint32_t old=profile?TEST_SN_SECTIONS:TEST_DS_SECTIONS;
        const uint32_t va=profile?TEST_SN_SECTIONS:TEST_DS_SECTIONS;
        const uint32_t node=profile?TEST_SN_NODE:TEST_DS_NODE;
        for(unsigned row=0;row<4u;row++) for(unsigned col=0;col<4u;col++) {
            static const unsigned offset[4]={0u,4u,8u,16u};
            const unsigned delta=row*28u+offset[col];
            virtual_put(&m,va+delta,virtual_get(&m,old+delta));
        }
        put(&m,node+176u,va);
        uint32_t pte=page_entry(&m,va);
        put(&m,pte,get(&m,pte)&~8u); /* Exercise the supplied mapped boundary. */
    }
    before=m;
    enum kui_armada_ce_live_status status=kui_armada_ce_live_sound_bind_with_reader(
        TEST_TTB,TEST_CCR,read_word,&m,loaded_layout_word,&bind_work,&out);
    assert(m.count==before.count && !memcmp(m.cells,before.cells,sizeof(m.cells)));
#ifdef KUI_ARMADA_PREVIOUS_LOADED_LAYOUT
    assert(status==KUI_ARMADA_CE_LIVE_MODULE && m.reads==204u &&
           bind_work.fault[0]==KUI_ARMADA_CE_LIVE_FAULT_MISSING &&
           !bind_work.dsound.node && !bind_work.sndcore.node &&
           bind_work.coredll.node==TEST_CORE_NODE && bind_work.links==16u);
    printf("Previous b9aa5c70a431 authored loaded-size replay: MODULE=4 READ=204 MISSING=14 DS=0 SN=0 CORE=%08x LINKS=16 QUEUE=0.\n",
           bind_work.coredll.node);
#else
    assert(status==KUI_ARMADA_CE_LIVE_OK && out.owner_index==2u &&
           out.device==(owner_slot(2u)|TEST_DEVICE) &&
           out.dsound_module==TEST_DS_NODE && out.sndcore_module==TEST_SN_NODE &&
           out.coredll_module==TEST_CORE_NODE && bind_work.links==16u &&
           m.reads<KUI_ARMADA_CE_LIVE_READ_LIMIT);
    printf("Current authored loaded-size replay: OK=0 READ=%u DS=%08x SN=%08x CORE=%08x LINKS=16; final metadata/CRC/native owner guards passed.\n",
           m.reads,out.dsound_module,out.sndcore_module,out.coredll_module);
#endif
    return 0;
}
