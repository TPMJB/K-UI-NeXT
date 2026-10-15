/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_stream_owner_word.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Authored metadata fixture. The native value and dirty physical-alias value
 * deliberately disagree; no game/module bytes or native memory are used. */
struct owner_fixture {
    uint32_t process,slot,handle,va,ttb,sr,p1,page,slot_pointer,block_pointer;
    uint32_t current,actual_slot,actual_handle,native_value,dirty_alias;
    uint32_t addresses[18],mutation_address,mutation_mask;
    unsigned table_reads,native_reads,alias_reads,fail_at,mutate_at;
    bool fail_native,mutate_native;
};
static unsigned checks,cases;
#define CHECK(condition) do { assert(condition);++checks; } while(0)
static struct owner_fixture fixture(unsigned owner,unsigned color,unsigned word) {
    struct owner_fixture f={0};
    CHECK(owner<32u && color<4u && word<8u);
    f.process=0x8c14e6e0u+owner*0x9cu;f.slot=(owner+1u)<<25;
    f.handle=0x10203040u+owner;f.va=f.slot|0x01458000u|(color<<12)|(word*4u);
    f.ttb=0x8c210000u;f.sr=0x400080f0u;
    f.p1=0x8c420000u|(((color+1u)&3u)<<12)|(word*4u);
    f.page=(f.p1&0x1ffff000u)|0x79u;
    f.slot_pointer=0x8c220000u;f.block_pointer=0x8c230000u;
    f.current=f.process;f.actual_slot=f.slot;f.actual_handle=f.handle;
    f.native_value=0x76543210u^owner^color^word;f.dirty_alias=~f.native_value;
    return f;
}
static bool table(void *context,uint32_t address,uint32_t *value) {
    struct owner_fixture *f=context;
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
    if(f->table_reads==f->fail_at) return false;
    *value=*source;
    if(f->table_reads==f->mutate_at) *value^=f->mutation_mask;
    return true;
}
static bool native(void *context,uint32_t address,uint32_t *value) {
    struct owner_fixture *f=context;
    CHECK(address==f->va && f->table_reads==9u && !f->native_reads);
    ++f->native_reads;
    if(f->fail_native) return false;
    *value=f->native_value;
    if(f->mutate_native) {
        if(f->mutation_address==0x8c145890u) f->current^=f->mutation_mask;
        else if(f->mutation_address==f->process+12u) f->actual_slot^=f->mutation_mask;
        else if(f->mutation_address==f->process+8u) f->actual_handle^=f->mutation_mask;
        else if(f->mutation_address==1u) f->slot_pointer^=f->mutation_mask;
        else if(f->mutation_address==2u) f->block_pointer^=f->mutation_mask;
        else {CHECK(f->mutation_address==3u);f->page^=f->mutation_mask;}
    }
    return true;
}
static enum kui_armada_ce_stream_owner_status run(struct owner_fixture *f,
    struct kui_armada_ce_ram_binding *out) {
    return kui_armada_ce_stream_owner_word(f->ttb,f->sr,f->process,f->slot,f->handle,
        f->va,table,native,f,out);
}
static void expect(struct owner_fixture *f,enum kui_armada_ce_stream_owner_status expected,
    bool sampled) {
    struct kui_armada_ce_ram_binding result={1u,2u,3u};
    CHECK(run(f,&result)==expected && !f->alias_reads);
    CHECK(f->native_reads==(sampled?1u:0u));
    if(expected==KUI_ARMADA_CE_STREAM_OWNER_OK) {
        CHECK(result.physical_address==f->p1 && result.page_word==f->page &&
            result.value==f->native_value && result.value!=f->dirty_alias);
        CHECK(f->table_reads==18u);
    } else CHECK(!result.physical_address && !result.page_word && !result.value);
    ++cases;
}
int main(void) {
    /* All exact owner slots, all VA index colors, and every word offset use
     * native VA data even when a dirty physical alias contains another word. */
    for(unsigned owner=0;owner<32u;owner++) for(unsigned color=0;color<4u;color++)
        for(unsigned word=0;word<8u;word++) {
            struct owner_fixture f=fixture(owner,color,word);
            expect(&f,KUI_ARMADA_CE_STREAM_OWNER_OK,true);
        }
    const uint32_t bad_sr[]={0u,0x40000000u,0x400000e0u,0x500000f0u,0x600000f0u,0x700000f0u};
    for(unsigned i=0;i<sizeof(bad_sr)/sizeof(*bad_sr);i++) {
        struct owner_fixture f=fixture(0,0,0);f.sr=bad_sr[i];
        expect(&f,KUI_ARMADA_CE_STREAM_OWNER_CONTEXT,false);CHECK(!f.table_reads);
    }
    const uint32_t bad_va[]={0u,0x01010000u,0x02000000u,0x0200fffcu,0x03458001u,
        0x05458000u,0x80000000u,0x8c420000u,0xac420000u,0xffffffffu};
    for(unsigned i=0;i<sizeof(bad_va)/sizeof(*bad_va);i++) {
        struct owner_fixture f=fixture(0,0,0);f.va=bad_va[i];
        expect(&f,KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT,false);CHECK(!f.table_reads);
    }
    for(unsigned field=0;field<4u;field++) {
        struct owner_fixture f=fixture(0,0,0);
        if(field==0u) f.process++;else if(field==1u) f.slot+=0x02000000u;
        else if(field==2u) f.handle=0u;else f.handle=UINT32_MAX;
        expect(&f,KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT,false);CHECK(!f.table_reads);
    }
    for(unsigned field=0;field<3u;field++) {
        struct owner_fixture f=fixture(0,0,0);
        if(field==0u) f.current^=4u;else if(field==1u) f.actual_slot^=0x02000000u;
        else f.actual_handle^=4u;
        expect(&f,KUI_ARMADA_CE_STREAM_OWNER_PROCESS,false);CHECK(f.table_reads==3u);
    }
    const uint32_t bad_ttb[]={0u,0x8c000000u,0xac210000u,0x8c210001u,0x8d000000u};
    for(unsigned i=0;i<sizeof(bad_ttb)/sizeof(*bad_ttb);i++) {
        struct owner_fixture f=fixture(0,0,0);f.ttb=bad_ttb[i];
        expect(&f,KUI_ARMADA_CE_STREAM_OWNER_TTB,false);CHECK(f.table_reads==3u);
    }
    for(unsigned field=0;field<4u;field++) {
        struct owner_fixture f=fixture(0,0,0);
        if(field==0u) f.slot_pointer=0xac220000u;
        else if(field==1u) f.block_pointer=0x00023000u;
        else if(field==2u) f.page&=~1u;
        else f.page=0x00800079u;
        enum kui_armada_ce_stream_owner_status expected[]={KUI_ARMADA_CE_STREAM_OWNER_SLOT,
            KUI_ARMADA_CE_STREAM_OWNER_BLOCK,KUI_ARMADA_CE_STREAM_OWNER_PAGE,
            KUI_ARMADA_CE_STREAM_OWNER_DESTINATION};
        expect(&f,expected[field],false);
    }
    /* Failure at each table read is bounded and never publishes half a lease.
     * Reads 1..9 precede native access; reads 10..18 follow it. */
    for(unsigned at=1u;at<=18u;at++) {
        struct owner_fixture f=fixture(0,0,0);f.fail_at=at;
        enum kui_armada_ce_stream_owner_status expected=at>=10u && at<=15u?
            KUI_ARMADA_CE_STREAM_OWNER_CHANGED:KUI_ARMADA_CE_STREAM_OWNER_READ;
        expect(&f,expected,at>9u);CHECK(f.table_reads==at);
    }
    struct owner_fixture f=fixture(0,0,0);f.fail_native=true;
    expect(&f,KUI_ARMADA_CE_STREAM_OWNER_NATIVE_READ,true);CHECK(f.table_reads==9u);
    /* Changes within the walk are refused before any native object read. */
    for(unsigned at=7u;at<=9u;at++) {
        f=fixture(0,0,0);f.mutate_at=at;f.mutation_mask=0x1000u;
        expect(&f,KUI_ARMADA_CE_STREAM_OWNER_CHANGED,false);CHECK(f.table_reads==at);
    }
    /* A native read callback can model process/handle/PFN changes. Each is
     * detected after the read; even the value already sampled is discarded. */
    const uint32_t changed[]={0x8c145890u,0x8c14e6ecu,0x8c14e6e8u,1u,2u,3u};
    for(unsigned field=0;field<sizeof(changed)/sizeof(*changed);field++) {
        f=fixture(0,0,0);f.mutate_native=true;f.mutation_address=changed[field];
        f.mutation_mask=field<3u?4u:field==5u?0x1000u:0x20000000u;
        expect(&f,KUI_ARMADA_CE_STREAM_OWNER_CHANGED,true);
    }
    for(unsigned bit=0;bit<32u;bit++) {
        f=fixture(0,0,0);f.mutate_native=true;f.mutation_address=3u;
        f.mutation_mask=UINT32_C(1)<<bit;
        expect(&f,KUI_ARMADA_CE_STREAM_OWNER_CHANGED,true);
    }
    struct kui_armada_ce_ram_binding out={1u,2u,3u};f=fixture(0,0,0);
    CHECK(kui_armada_ce_stream_owner_word(f.ttb,f.sr,f.process,f.slot,f.handle,f.va,
        NULL,native,&f,&out)==KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT && !out.value && !f.table_reads);
    CHECK(kui_armada_ce_stream_owner_word(f.ttb,f.sr,f.process,f.slot,f.handle,f.va,
        table,NULL,&f,&out)==KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT && !out.value && !f.table_reads);
    CHECK(run(&f,NULL)==KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT && !f.table_reads);
    /* Actual SH integer arithmetic changes T/Q/M during a valid native
     * read. These changes must not masquerade as a privilege/IRQ change. */
    CHECK(kui_armada_ce_stream_owner_sr_stable(0x400000f0u,0x400000f1u));
    CHECK(kui_armada_ce_stream_owner_sr_stable(0x400000f0u,0x400003f0u));
    CHECK(kui_armada_ce_stream_owner_sr_stable(0x400000f1u,0x400003f0u));
    CHECK(!kui_armada_ce_stream_owner_sr_stable(0x400000f0u,0x000000f0u));
    CHECK(!kui_armada_ce_stream_owner_sr_stable(0x400000f0u,0x600000f0u));
    CHECK(!kui_armada_ce_stream_owner_sr_stable(0x400000f0u,0x500000f0u));
    for(unsigned irq=0;irq<15u;irq++)
        CHECK(!kui_armada_ce_stream_owner_sr_stable(0x400000f0u,0x40000000u|(irq<<4)));
    printf("Armada CE owner word: %u cases / %u checks; native VA only, bounded18 metadata reads, owner/PFN changes rejected.\n",cases,checks);
    return 0;
}
