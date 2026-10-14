/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse authored CRC-correct metadata, never private executable bytes. */
#define main kui_armada_ce_live_sound_fixture_main
#include "test_armada_ce_live_sound.c"
#undef main
#include "kui/armada_ce_stream_initial_word.h"
#include "kui/armada_ce_stream_rebind_word.h"

struct initial_fixture {
    struct test_memory memory;
    struct kui_armada_ce_stream_initial_owner owner;
    struct kui_armada_ce_live_workspace workspace;
    unsigned metadata_reads,native_reads,mapped_calls;
    bool change_handle;
};
static bool initial_table(void *context,uint32_t address,uint32_t *value) {
    struct initial_fixture *f=context;
    uint32_t p1;
    assert(kui_armada_ce_ram_table_p1(address,&p1) && p1==address);
    /* All fixture tables/process/module records are below native backing.
     * A physical value observation would violate this ledger. */
    assert(address<0x8c800000u);
    ++f->metadata_reads;
    return lookup(&f->memory,address,value);
}
static bool initial_outer(void *context,uint32_t address,uint32_t *value) {
    struct initial_fixture *f=context;
    assert(address<0x8c800000u);
    return read_word(&f->memory,address,value);
}
static bool initial_native(void *context,uint32_t va,uint32_t *value) {
    struct initial_fixture *f=context;
    assert(kui_armada_ce_stream_initial_address(&f->owner,&f->workspace,va));
    ++f->native_reads;
    if((va&0xfffff000u)==f->memory.cache_va) {
        uint32_t p1=physical(&f->memory,va),color=(va>>12)&3u;
        *value=get(&f->memory,0xf5000000u|(p1&0xfe0u)|(color<<12)|(p1&0x1cu));
        assert(*value!=get(&f->memory,p1));
    } else *value=virtual_get(&f->memory,va);
    if(f->change_handle) put(&f->memory,f->owner.process+8u,f->owner.handle^4u);
    return true;
}
static enum kui_armada_ce_ram_bind_status initial_mapped(void *context,uint32_t va,
    uint32_t p1,uint32_t page,uint32_t ccr,uint32_t *value) {
    struct initial_fixture *f=context;
    struct kui_armada_ce_ram_binding observed;
    *value=0;
    if(!kui_armada_ce_stream_initial_address(&f->owner,&f->workspace,va))
        return KUI_ARMADA_CE_RAM_BIND_ARGUMENT;
    assert(ccr==TEST_CCR);
    unsigned before=f->metadata_reads;
    ++f->mapped_calls;
    enum kui_armada_ce_stream_owner_status status=kui_armada_ce_stream_rebind_word(
        TEST_TTB,0x400880f0u,f->owner.process,f->owner.slot,f->owner.handle,
        va,p1,page,initial_table,initial_native,f,&observed);
    assert(f->metadata_reads-before==18u);
    if(status!=KUI_ARMADA_CE_STREAM_OWNER_OK) return KUI_ARMADA_CE_RAM_BIND_CHANGED;
    *value=observed.value;
    return KUI_ARMADA_CE_RAM_BIND_OK;
}
static void initial_fixture(struct initial_fixture *f) {
    memset(f,0,sizeof(*f));
    cache_section_fixture(&f->memory,5u);
    put(&f->memory,owner_process(5u)+8u,0x12345679u);
    assert(kui_armada_ce_stream_initial_owner(TEST_TTB,TEST_CCR,0x400880f0u,
        initial_table,f,&f->owner));
    assert(f->metadata_reads==8u && f->owner.process==0x8c14e9ecu &&
        f->owner.slot==0x0c000000u && f->owner.index==5u);
}
static enum kui_armada_ce_live_status initial_bind(struct initial_fixture *f,
    struct kui_armada_ce_live_sound *out) {
    enum kui_armada_ce_live_status status=kui_armada_ce_live_sound_bind_with_value_reader(
        TEST_TTB,TEST_CCR,initial_outer,f,initial_mapped,true,&f->workspace,out);
    assert(!f->workspace.native_candidate);
    assert(f->workspace.reads==f->memory.reads && f->memory.reads<=TEST_READS);
    assert(f->metadata_reads==8u+18u*f->mapped_calls && f->mapped_calls==f->native_reads);
    assert(!f->memory.cache_reads && !f->memory.cache_calls);
    return status;
}
int main(void) {
    static struct initial_fixture f;
    struct kui_armada_ce_live_sound out;
    unsigned checks=0;
    /* The initial legacy cache observer refuses its changing shared word.
     * The same authored module/owner fixture succeeds using native values. */
    initial_fixture(&f);
    uint32_t p1=physical(&f.memory,TEST_DS_SECTIONS),index=p1&0xfe0u;
    f.memory.cache_change_address=0xf4000000u|index;
    f.memory.cache_change_on=2u;f.memory.cache_change_mask=1u;
    assert(bind_coherent(&f.memory,&out)==KUI_ARMADA_CE_LIVE_CHANGED);
    assert(bind_work.fault[1]==TEST_DS_SECTIONS &&
        bind_work.fault[2]==KUI_ARMADA_CE_RAM_BIND_CHANGED);++checks;
    initial_fixture(&f);
    assert(initial_bind(&f,&out)==KUI_ARMADA_CE_LIVE_OK);
    assert(out.current_process==f.owner.process && out.owner_process==f.owner.process &&
        out.device==(f.owner.slot|TEST_DEVICE) && f.native_reads>0u);++checks;
    /* Shared access is exact module scope, never a general slot1 reader. */
    assert(kui_armada_ce_stream_initial_address(&f.owner,&f.workspace,0x03cd1104u));
    assert(!kui_armada_ce_stream_initial_address(&f.owner,&f.workspace,0x03cdb000u));
    assert(!kui_armada_ce_stream_initial_address(&f.owner,&f.workspace,0x03aa1000u));
    assert(!kui_armada_ce_stream_initial_address(&f.owner,&f.workspace,0x060995d4u));++checks;
    f.workspace.dsound.attach^=UINT32_C(1)<<5;
    assert(!kui_armada_ce_stream_initial_address(&f.owner,&f.workspace,0x03cd1104u));++checks;
    /* The initial reader does not waive identity or owner-generation guards. */
    initial_fixture(&f);
    virtual_put(&f.memory,f.owner.slot|0x995d4u,
        virtual_get(&f.memory,f.owner.slot|0x995d4u)^1u);
    assert(initial_bind(&f,&out)==KUI_ARMADA_CE_LIVE_OWNER);++checks;
    initial_fixture(&f);f.change_handle=true;
    assert(initial_bind(&f,&out)==KUI_ARMADA_CE_LIVE_CHANGED);++checks;
    printf("Armada CE initial native binding: %u focused checks; legacy shared CACHE CHANGED reproduced, native owner proof and all identity guards retained.\n",checks);
    return 0;
}
