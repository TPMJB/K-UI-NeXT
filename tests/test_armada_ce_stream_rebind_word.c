/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_stream_rebind_word.h"
#include "kui/armada_ce_live_sound.h"
#include <assert.h>
#include <stdio.h>

/* Authored metadata only. A dirty physical alias deliberately disagrees
 * with the native shared-VA word; neither value comes from module bytes. */
struct rebind_fixture {
    uint32_t process,owner_slot,handle,va,ttb,sr,p1,page,slot_pointer,block_pointer;
    uint32_t current,actual_slot,actual_handle,native_value,dirty_alias;
    uint32_t expected_p1,expected_page,addresses[18];
    unsigned table_reads,native_reads,alias_reads;
    bool change_pfn,change_handle,change_walk,fail_native;
};
static unsigned checks,cases;
#define CHECK(condition) do { assert(condition);++checks; } while(0)

static struct rebind_fixture fixture(void) {
    struct rebind_fixture f={0};
    f.process=0x8c14e6e0u+2u*0x9cu;f.owner_slot=0x06000000u;
    f.handle=0x10203042u;f.va=0x03cd2e5cu;
    f.ttb=0x8c210000u;f.sr=0x400080f0u;
    f.p1=0x8c427e5cu;f.page=(f.p1&0x1ffff000u)|0x79u;
    f.slot_pointer=0x8c220000u;f.block_pointer=0x8c230000u;
    f.current=f.process;f.actual_slot=f.owner_slot;f.actual_handle=f.handle;
    f.native_value=0x76543210u;f.dirty_alias=~f.native_value;
    f.expected_p1=f.p1;f.expected_page=f.page;
    return f;
}
static bool table(void *context,uint32_t address,uint32_t *value) {
    struct rebind_fixture *f=context;
    const uint32_t slot_at=f->ttb+4u*(f->va>>25);
    const uint32_t block_at=f->slot_pointer+4u*((f->va>>16)&0x1ffu);
    const uint32_t page_at=f->block_pointer+12u+4u*((f->va>>12)&15u);
    uint32_t *source=NULL,canonical;
    CHECK(kui_armada_ce_ram_table_p1(address,&canonical) && address==canonical);
    if(address==0x8c145890u) source=&f->current;
    else if(address==f->process+12u) source=&f->actual_slot;
    else if(address==f->process+8u) source=&f->actual_handle;
    else if(address==slot_at) source=&f->slot_pointer;
    else if(address==block_at) source=&f->block_pointer;
    else if(address==page_at) source=&f->page;
    else if(address==f->p1) {++f->alias_reads;source=&f->dirty_alias;}
    CHECK(source && f->table_reads<18u);
    f->addresses[f->table_reads++]=address;
    *value=*source;
    if(f->change_walk && f->table_reads==9u) *value^=0x1000u;
    return true;
}
static bool native(void *context,uint32_t address,uint32_t *value) {
    struct rebind_fixture *f=context;
    CHECK(address==f->va && f->table_reads==9u && !f->native_reads);
    ++f->native_reads;
    if(f->fail_native) return false;
    *value=f->native_value;
    if(f->change_pfn) f->page^=0x1000u;
    if(f->change_handle) f->actual_handle^=4u;
    return true;
}
static enum kui_armada_ce_stream_owner_status run(struct rebind_fixture *f,
    struct kui_armada_ce_ram_binding *out) {
    return kui_armada_ce_stream_rebind_word(f->ttb,f->sr,f->process,f->owner_slot,
        f->handle,f->va,f->expected_p1,f->expected_page,table,native,f,out);
}
static void expect(struct rebind_fixture *f,enum kui_armada_ce_stream_owner_status expected,
    unsigned table_reads,bool sampled) {
    struct kui_armada_ce_ram_binding out={1u,2u,3u};
    CHECK(run(f,&out)==expected);
    CHECK(f->table_reads==table_reads && f->native_reads==(sampled?1u:0u));
    CHECK(!f->alias_reads);
    if(expected==KUI_ARMADA_CE_STREAM_OWNER_OK) {
        const uint32_t slot_at=f->ttb+4u*(f->va>>25);
        const uint32_t block_at=f->slot_pointer+4u*((f->va>>16)&0x1ffu);
        const uint32_t page_at=f->block_pointer+12u+4u*((f->va>>12)&15u);
        const uint32_t addresses[]={0x8c145890u,f->process+12u,f->process+8u,
            slot_at,block_at,page_at,slot_at,block_at,page_at,
            slot_at,block_at,page_at,slot_at,block_at,page_at,
            0x8c145890u,f->process+12u,f->process+8u};
        CHECK(out.physical_address==f->expected_p1 && out.page_word==f->expected_page &&
            out.value==f->native_value && out.value!=f->dirty_alias);
        for(unsigned i=0;i<18u;i++) CHECK(f->addresses[i]==addresses[i]);
    } else CHECK(!out.physical_address && !out.page_word && !out.value);
    ++cases;
}
struct policy_fixture {
    struct rebind_fixture word;
    unsigned outer_reads,alias_reads,mapped_calls;
};
static bool policy_table(void *context,uint32_t address,uint32_t *value) {
    struct policy_fixture *p=context;
    struct rebind_fixture *f=&p->word;
    uint32_t canonical;
    CHECK(kui_armada_ce_ram_table_p1(address,&canonical) && address==canonical);
    CHECK(p->outer_reads<7u);++p->outer_reads;
    if(address==f->ttb+4u*(f->va>>25)) *value=f->slot_pointer;
    else if(address==f->slot_pointer+4u*((f->va>>16)&0x1ffu)) *value=f->block_pointer;
    else if(address==f->block_pointer+12u+4u*((f->va>>12)&15u)) *value=f->page;
    else {CHECK(address==f->p1);++p->alias_reads;*value=f->dirty_alias;}
    return true;
}
static enum kui_armada_ce_ram_bind_status policy_mapped(void *context,uint32_t va,
    uint32_t p1,uint32_t page,uint32_t ccr,uint32_t *value) {
    struct policy_fixture *p=context;
    struct rebind_fixture *f=&p->word;
    struct kui_armada_ce_ram_binding out;
    CHECK(va==f->va && p1==f->expected_p1 && page==f->expected_page && ccr==1u);
    CHECK(!p->mapped_calls && p->outer_reads==6u);++p->mapped_calls;
    enum kui_armada_ce_stream_owner_status status=kui_armada_ce_stream_rebind_word(
        f->ttb,f->sr,f->process,f->owner_slot,f->handle,va,p1,page,table,native,f,&out);
    CHECK(status==KUI_ARMADA_CE_STREAM_OWNER_OK);
    *value=out.value;return KUI_ARMADA_CE_RAM_BIND_OK;
}
static void coherent_policy(bool native_values) {
    struct policy_fixture p={.word=fixture()};
    p.word.p1=(p.word.p1&~0x3000u)|(p.word.va&0x3000u);
    p.word.page=(p.word.p1&0x1ffff000u)|0x79u;
    p.word.expected_p1=p.word.p1;p.word.expected_page=p.word.page;
    struct kui_armada_ce_live_workspace w={0};
    w.read32=policy_table;w.mapped32=policy_mapped;w.context=&p;
    w.ttb=p.word.ttb;w.ccr=1u;w.native_values=native_values;
    w.failure=KUI_ARMADA_CE_LIVE_READ;
    uint32_t value=0;
    CHECK(!((p.word.va^p.word.p1)&0x3000u));
    CHECK(kui_armada_ce_live_word(&w,p.word.va,&value)==KUI_ARMADA_CE_LIVE_OK);
    CHECK(w.pages==1u && w.page[0].va==(p.word.va&0xfffff000u) &&
        w.page[0].word==p.word.page && w.reads==p.outer_reads);
    if(native_values) {
        CHECK(value==p.word.native_value && value!=p.word.dirty_alias);
        CHECK(p.mapped_calls==1u && p.outer_reads==6u && !p.alias_reads);
        CHECK(p.word.table_reads==18u && p.word.native_reads==1u && !p.word.alias_reads);
    } else {
        CHECK(value==p.word.dirty_alias && value!=p.word.native_value);
        CHECK(!p.mapped_calls && p.outer_reads==7u && p.alias_reads==1u);
        CHECK(!p.word.table_reads && !p.word.native_reads);
    }
    ++cases;
}
int main(void) {
    /* Shared slot 1 stays a native read in slot 3's retained owner context. */
    struct rebind_fixture f=fixture();
    CHECK((f.va&0xfe000000u)==0x02000000u && f.owner_slot==0x06000000u);
    CHECK((f.va^f.p1)&0x3000u);
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_OK,18u,true);

    /* The exact owner slot is still admitted, with the same bounded walk. */
    f=fixture();f.va=f.owner_slot|(f.va&0x01ffffffu);
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_OK,18u,true);

    f=fixture();f.va=0x09cd2e5cu;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT,0u,false);
    f=fixture();f.sr=0x600080f0u;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_CONTEXT,0u,false);
    f=fixture();f.process++;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT,0u,false);
    f=fixture();f.owner_slot=0x08000000u;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT,0u,false);
    f=fixture();f.handle=0u;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT,0u,false);

    /* The current owner, its slot and its handle must all match. */
    f=fixture();f.current^=4u;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_PROCESS,3u,false);
    f=fixture();f.actual_slot=0x08000000u;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_PROCESS,3u,false);
    f=fixture();f.actual_handle^=4u;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_PROCESS,3u,false);

    /* A retained witness mismatch is refused before native value sampling. */
    f=fixture();f.expected_p1^=0x1000u;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_CHANGED,9u,false);
    f=fixture();f.expected_page^=0x1000u;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_CHANGED,9u,false);
    f=fixture();f.change_walk=true;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_CHANGED,9u,false);

    /* A stable second walk may still name a different PFN. Discard the
     * native value if either that witness or the owner's handle changed. */
    f=fixture();f.change_pfn=true;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_CHANGED,15u,true);
    f=fixture();f.change_handle=true;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_CHANGED,18u,true);
    f=fixture();f.fail_native=true;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_NATIVE_READ,9u,true);

    /* Matching cache colors cannot bypass the worker's native policy.
     * The established coherent-color policy still samples its P1 alias. */
    coherent_policy(true);
    coherent_policy(false);

    printf("Armada CE rebind word: %u cases / %u checks; shared and owner native VA reads, bounded18 metadata, retained mapping required.\n",cases,checks);
    return 0;
}
