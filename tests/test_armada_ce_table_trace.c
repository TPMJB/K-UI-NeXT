/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_table_trace.h"
#include <assert.h>
#include <stdio.h>

struct memory {
    uint32_t root_at,root_value,block_address,block_value;
    unsigned reads,fail_at,change_at;
};
static bool read_word(void *context,uint32_t address,uint32_t *value) {
    struct memory *m=context;uint32_t unchanged;
    assert(kui_armada_ce_ram_table_p1(address,&unchanged) && unchanged==address);
    ++m->reads;
    if(m->reads==m->fail_at) return false;
    if(address==m->root_at) *value=m->root_value;
    else if(address==m->block_address) *value=m->block_value;
    else {assert(!"MEMBLOCK/page/holder or unrelated address was read");return false;}
    if(m->reads==m->change_at) *value^=4u;
    return true;
}
static struct memory fixture(uint32_t ttb,uint32_t va,uint32_t slot,uint32_t block) {
    return (struct memory){.root_at=ttb+4u*(va>>25),.root_value=slot,
        .block_address=slot+4u*((va>>16)&511u),.block_value=block};
}
int main(void) {
    const uint32_t ttb=0x8c1458a0u,va=KUI_ARMADA_CE_HEAP_NOMINAL;
    const uint32_t slot=0x8c180000u,block=0x8c190000u;
    struct kui_armada_ce_table_trace out;
    unsigned cases=0;
    for(unsigned i=0;i<KUI_ARMADA_CE_TTB_SLOTS;i++) {
        uint32_t address=(i<<25)|va;
        struct memory m=fixture(ttb,address,slot,block);
        assert(kui_armada_ce_trace_table(ttb,address,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_OK);
        assert(out.slot_value==slot && out.block_address==slot+0x754u && out.block_value==block && m.reads==4);++cases;
    }
    const uint32_t raw_blocks[]={0u,1u,0x16u,0x5800u,0x0c190000u,0xac190000u,
        0xec190000u,0x8c000000u,0x8d000000u,0x8c190001u};
    for(unsigned i=0;i<sizeof(raw_blocks)/sizeof(raw_blocks[0]);i++) {
        struct memory m=fixture(ttb,va,slot,raw_blocks[i]);
        assert(kui_armada_ce_trace_table(ttb,va,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_BLOCK);
        assert(out.slot_value==slot && out.block_address==m.block_address && out.block_value==raw_blocks[i] && m.reads==4);++cases;
    }
    /* The kernel's exact absent-slot sentinel leads to a zero table entry;
     * its positive value is evidence only, never a guessed physical pointer. */
    struct memory m=fixture(ttb,va,KUI_ARMADA_CE_EMPTY_SLOT_TABLE,0);
    assert(kui_armada_ce_trace_table(ttb,va,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_BLOCK);
    assert(out.slot_value==0x8c012b0cu && out.block_address==0x8c013260u && !out.block_value && m.reads==4);++cases;
    const uint32_t raw_slots[]={0u,0x0c180000u,0xac180000u,0xec180000u,
        0x8c000000u,0x8d000000u,0x8c180001u,0x8cfffffcu};
    for(unsigned i=0;i<sizeof(raw_slots)/sizeof(raw_slots[0]);i++) {
        m=fixture(ttb,va,raw_slots[i],block);
        assert(kui_armada_ce_trace_table(ttb,va,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_SLOT);
        assert(out.slot_value==raw_slots[i] && !out.block_address && !out.block_value && m.reads==2);++cases;
    }
    for(unsigned i=1;i<=4;i++) {
        m=fixture(ttb,va,slot,block);m.fail_at=i;
        assert(kui_armada_ce_trace_table(ttb,va,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_READ);
        assert(m.reads==i);++cases;
    }
    for(unsigned i=3;i<=4;i++) {
        m=fixture(ttb,va,slot,block);m.change_at=i;
        assert(kui_armada_ce_trace_table(ttb,va,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_CHANGED);
        assert(out.slot_value==slot && out.block_value==block && m.reads==i);++cases;
    }
    m=fixture(ttb,va,0xac180000u,block);m.change_at=2;
    assert(kui_armada_ce_trace_table(ttb,va,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_CHANGED);
    assert(out.slot_value==0xac180000u && !out.block_address && m.reads==2);++cases;
    const uint32_t bad_roots[]={0u,0x8c000000u,0xac1458a0u,0x8d000000u,0xff000008u,0x8c1458a1u};
    for(unsigned i=0;i<sizeof(bad_roots)/sizeof(bad_roots[0]);i++) {
        m=(struct memory){0};
        assert(kui_armada_ce_trace_table(bad_roots[i],va,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_TTB);
        assert(!m.reads && !out.slot_value && !out.block_address && !out.block_value);++cases;
    }
    m=(struct memory){0};
    assert(kui_armada_ce_trace_table(ttb,va|1u,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);++cases;
    assert(kui_armada_ce_trace_table(ttb,0x80000000u,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);++cases;
    assert(kui_armada_ce_trace_table(ttb,va,NULL,&m,&out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);++cases;
    assert(kui_armada_ce_trace_table(ttb,va,read_word,&m,NULL)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);++cases;
    assert(!m.reads);
    printf("Armada CE raw table trace: %u cases passed; no unsupported block was dereferenced.\n",cases);
    return 0;
}
