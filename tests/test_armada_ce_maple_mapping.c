/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_maple_mapping.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct cell {uint32_t address,value;unsigned visits;};
struct fixture {
    struct cell cell[3];
    unsigned calls,fail_at,change_cell,change_visit,unexpected;
};
static bool read_word(void *context,uint32_t address,uint32_t *value) {
    struct fixture *f=context;
    ++f->calls;
    if(f->calls==f->fail_at) return false;
    for(unsigned i=0;i<3u;i++) if(f->cell[i].address==address) {
        ++f->cell[i].visits;*value=f->cell[i].value;
        if(f->change_cell==i+1u && f->cell[i].visits==f->change_visit) *value^=4u;
        return true;
    }
    ++f->unexpected;return false;
}
/* Independent concrete witnesses: observed TTB/base, hypothetical P1 table
 * allocations, and the exact ROM completion backing. The reader admits only
 * these three table words, so any backing/MMIO/cache read fails the test. */
static struct fixture completion(void) {
    const struct fixture f={.cell={
        {0x8c1458a4u,0x8c300000u,0u},
        {0x8c300774u,0x8c400000u,0u},
        {0x8c400020u,0x0c0b3009u,0u}}};
    return f;
}
static enum kui_armada_ce_ram_bind_status resolve(struct fixture *f,
    struct kui_armada_ce_maple_mapping *out) {
    enum kui_armada_ce_ram_bind_status status=kui_armada_ce_maple_resolve(
        0x8c1458a0u,0x03dd5b0cu,read_word,f,out);
    assert(f->calls<=6u && !f->unexpected);
    if(status!=KUI_ARMADA_CE_RAM_BIND_OK) assert(!out->physical_address);
    return status;
}
static unsigned zero_refusals(void) {
    /* Poison every field and both adjacent words: an early refusal must
     * leave no stale witness or write beyond the aligned mapping object. */
    struct guarded {
        uint32_t before;
        struct kui_armada_ce_maple_mapping map;
        uint32_t after;
    } guard;
    unsigned cases=0;
    for(unsigned failure=0;failure<=3u;failure++) {
        memset(&guard,0xa5,sizeof(guard));struct fixture f=completion();
        uint32_t va=failure?0x03dd5b0cu:0x01dd5b0cu;
        f.fail_at=failure;
        unsigned status=kui_armada_ce_maple_resolve(0x8c1458a0u,va,read_word,&f,&guard.map);
        assert(status==(failure?KUI_ARMADA_CE_RAM_BIND_READ:KUI_ARMADA_CE_RAM_BIND_ARGUMENT));
        assert(guard.before==0xa5a5a5a5u && guard.after==0xa5a5a5a5u);
        assert(guard.map.virtual_address==va && guard.map.physical_address==0u);
        if(failure<2u) assert(guard.map.slot_value==0u && guard.map.block_address==0u);
        if(failure<3u) assert(guard.map.block_value==0u && guard.map.page_address==0u);
        assert(guard.map.page_word==0u && f.calls==failure);
        if(!failure) assert(guard.map.slot_address==0u);
        ++cases;
    }
    memset(&guard,0xa5,sizeof(guard));struct fixture f=completion();
    assert(kui_armada_ce_maple_resolve(0xac1458a0u,0x03dd5b0cu,read_word,&f,&guard.map)==KUI_ARMADA_CE_RAM_BIND_TTB);
    assert(guard.map.virtual_address==0x03dd5b0cu && !f.calls);
    assert(!guard.map.slot_address && !guard.map.slot_value && !guard.map.block_address &&
        !guard.map.block_value && !guard.map.page_address && !guard.map.page_word && !guard.map.physical_address);
    assert(guard.before==0xa5a5a5a5u && guard.after==0xa5a5a5a5u);
    return cases+1u;
}
int main(void) {
    unsigned cases=zero_refusals();
    struct fixture f=completion();struct kui_armada_ce_maple_mapping out;
    assert(resolve(&f,&out)==KUI_ARMADA_CE_RAM_BIND_OK);
    assert(out.virtual_address==0x03dd5b0cu && out.slot_address==0x8c1458a4u);
    assert(out.slot_value==0x8c300000u && out.block_address==0x8c300774u);
    assert(out.block_value==0x8c400000u && out.page_address==0x8c400020u);
    assert(out.page_word==0x0c0b3009u && out.physical_address==0x8c0b3b0cu);
    assert(f.calls==6u);for(unsigned i=0;i<3u;i++) assert(f.cell[i].visits==2u);
    ++cases;
    /* The IAT page shares the same slot/block but has a different page index
     * and allocated physical backing; no fixed ROM data address is invented. */
    f=completion();f.cell[2]=(struct cell){0x8c400028u,0x0c234001u,0u};
    assert(kui_armada_ce_maple_resolve(0x8c1458a0u,0x03dd70acu,read_word,&f,&out)==KUI_ARMADA_CE_RAM_BIND_OK);
    assert(out.page_address==0x8c400028u && out.physical_address==0x8c2340acu);
    assert(f.calls==6u && !f.unexpected);++cases;
    for(unsigned call=1u;call<=6u;call++) {
        f=completion();f.fail_at=call;
        assert(resolve(&f,&out)==KUI_ARMADA_CE_RAM_BIND_READ && f.calls==call);++cases;
    }
    for(unsigned cell=1u;cell<=3u;cell++) {
        f=completion();f.change_cell=cell;f.change_visit=2u;
        assert(resolve(&f,&out)==KUI_ARMADA_CE_RAM_BIND_CHANGED);
        assert(f.calls==3u+cell);++cases;
    }
    static const uint32_t bad_va[]={0u,0x01dd5b0cu,0x01fffffcu,0x03dd5b0du,
        0x80000000u,0x8c0b3b0cu,0xac0b3b0cu,0xfffffffcu};
    for(unsigned i=0;i<sizeof(bad_va)/sizeof(*bad_va);i++) {
        f=completion();out=(struct kui_armada_ce_maple_mapping){.physical_address=~0u};
        assert(kui_armada_ce_maple_resolve(0x8c1458a0u,bad_va[i],read_word,&f,&out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
        assert(out.virtual_address==bad_va[i] && !out.physical_address && !f.calls);++cases;
    }
    static const uint32_t bad_ttb[]={0u,0x0c1458a0u,0xac1458a0u,0xcc1458a0u,
        0x8c00fffcu,0x8d000000u,0x8c1458a1u,0xfffffffcu,0x8cfffffcu};
    for(unsigned i=0;i<sizeof(bad_ttb)/sizeof(*bad_ttb);i++) {
        f=completion();
        assert(kui_armada_ce_maple_resolve(bad_ttb[i],0x03dd5b0cu,read_word,&f,&out)==KUI_ARMADA_CE_RAM_BIND_TTB);
        assert(!out.physical_address && !f.calls);++cases;
    }
    static const uint32_t bad_slot[]={0u,0x0c300000u,0xac300000u,0xcc300000u,
        0x8c00fffcu,0x8d000000u,0x8c300002u,0xfffffffcu,0x8cfffffcu};
    for(unsigned i=0;i<sizeof(bad_slot)/sizeof(*bad_slot);i++) {
        f=completion();f.cell[0].value=bad_slot[i];
        assert(resolve(&f,&out)==KUI_ARMADA_CE_RAM_BIND_SLOT);
        assert(out.slot_value==bad_slot[i] && f.calls==2u && !out.block_value);++cases;
    }
    f=completion();f.cell[0].value=0u;f.change_cell=1u;f.change_visit=2u;
    assert(resolve(&f,&out)==KUI_ARMADA_CE_RAM_BIND_CHANGED && f.calls==2u);++cases;
    f=completion();f.cell[0].value=0u;f.fail_at=2u;
    assert(resolve(&f,&out)==KUI_ARMADA_CE_RAM_BIND_READ && f.calls==2u);++cases;
    static const uint32_t bad_block[]={0u,1u,0x0c400000u,0xac400000u,0xcc400000u,
        0x8c00fffcu,0x8d000000u,0x8c400002u,0xfffffffcu,0x8cfffffcu};
    for(unsigned i=0;i<sizeof(bad_block)/sizeof(*bad_block);i++) {
        f=completion();f.cell[1].value=bad_block[i];
        assert(resolve(&f,&out)==KUI_ARMADA_CE_RAM_BIND_BLOCK);
        assert(out.block_value==bad_block[i] && f.calls==4u && !out.page_word);++cases;
    }
    for(unsigned cell=1u;cell<=2u;cell++) {
        f=completion();f.cell[1].value=1u;f.change_cell=cell;f.change_visit=2u;
        assert(resolve(&f,&out)==KUI_ARMADA_CE_RAM_BIND_CHANGED && f.calls==2u+cell);++cases;
    }
    for(unsigned call=3u;call<=4u;call++) {
        f=completion();f.cell[1].value=1u;f.fail_at=call;
        assert(resolve(&f,&out)==KUI_ARMADA_CE_RAM_BIND_READ && f.calls==call);++cases;
    }
    f=completion();f.cell[2].value=0x0c0b3008u;
    assert(resolve(&f,&out)==KUI_ARMADA_CE_RAM_BIND_PAGE && f.calls==6u);
    assert(out.page_word==0x0c0b3008u);++cases;
    static const uint32_t bad_page[]={1u,0x0c000001u,0x0d000001u,0x005f6001u,0x40000001u};
    for(unsigned i=0;i<sizeof(bad_page)/sizeof(*bad_page);i++) {
        f=completion();f.cell[2].value=bad_page[i];
        assert(resolve(&f,&out)==KUI_ARMADA_CE_RAM_BIND_DESTINATION && f.calls==6u);
        assert(out.page_word==bad_page[i]);++cases;
    }
    /* Exact admitted first/last words and maximum indices. Derived pointer
     * additions stay bounded; a one-word-later root was refused above. */
    f=(struct fixture){.cell={{0x8cfffffcu,0x8c300000u,0u},
        {0x8c3007fcu,0x8c400000u,0u},{0x8c400048u,0x0cfff001u,0u}}};
    assert(kui_armada_ce_maple_resolve(0x8cffff00u,0x7ffffffcu,read_word,&f,&out)==KUI_ARMADA_CE_RAM_BIND_OK);
    assert(out.slot_address==0x8cfffffcu && out.physical_address==0x8cfffffcu);
    assert(f.calls==6u && !f.unexpected);++cases;
    f=(struct fixture){.cell={{0x8c1458a4u,0x8c300000u,0u},
        {0x8c300000u,0x8c400000u,0u},{0x8c40000cu,0x0c010001u,0u}}};
    assert(kui_armada_ce_maple_resolve(0x8c1458a0u,0x02000000u,read_word,&f,&out)==KUI_ARMADA_CE_RAM_BIND_OK);
    assert(out.physical_address==0x8c010000u && f.calls==6u && !f.unexpected);++cases;
    f=completion();
    assert(kui_armada_ce_maple_resolve(0x8c1458a0u,0x03dd5b0cu,NULL,&f,&out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
    assert(!out.physical_address && !f.calls);++cases;
    assert(kui_armada_ce_maple_resolve(0x8c1458a0u,0x03dd5b0cu,read_word,&f,NULL)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
    assert(!f.calls);++cases;
    printf("Armada CE Maple mapping: %u cases passed\n",cases);
    return 0;
}
