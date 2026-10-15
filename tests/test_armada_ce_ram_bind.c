/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_ram_bind.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct cell {uint32_t address,value;};
struct memory {struct cell cells[16];unsigned count,reads,fail_at,change_at;};
static void put(struct memory *m,uint32_t address,uint32_t value) {
    for(unsigned i=0;i<m->count;i++) if(m->cells[i].address==address) {m->cells[i].value=value;return;}
    assert(m->count<16);m->cells[m->count++]=(struct cell){address,value};
}
static bool read_word(void *context,uint32_t address,uint32_t *value) {
    struct memory *m=context;uint32_t p1;
    assert(kui_armada_ce_ram_p1(address,&p1) && p1==address);
    ++m->reads;
    if(m->reads==m->fail_at) return false;
    for(unsigned i=0;i<m->count;i++) if(m->cells[i].address==address) {
        *value=m->cells[i].value^(m->reads==m->change_at?4u:0u);return true;
    }
    return false;
}
static struct memory fixture(uint32_t va,uint32_t ttb,uint32_t slot,uint32_t block,uint32_t page) {
    struct memory m={0};uint32_t root,slot_p1,block_p1,dest;
    assert(kui_armada_ce_ram_p1(ttb,&root));assert(kui_armada_ce_ram_p1(slot,&slot_p1));
    assert(kui_armada_ce_ram_p1(block,&block_p1));
    put(&m,root+4u*(va>>25),slot);
    put(&m,slot_p1+4u*((va>>16)&511u),block);
    put(&m,block_p1+12u+4u*((va>>12)&15u),page);
    if(kui_armada_ce_ram_p1((page&0xfffff000u)|(va&4095u)|0x80000000u,&dest))
        put(&m,dest,0x000f1234u);
    return m;
}
static void refused(struct memory *m,uint32_t ttb,uint32_t va,
                    enum kui_armada_ce_ram_bind_status status,unsigned reads) {
    struct kui_armada_ce_ram_binding out={1,2,3};
    assert(kui_armada_ce_ram_bind(ttb,va,0x105u,read_word,m,&out)==status);
    assert(out.physical_address==0 && out.value==0 && out.page_word==0);
    assert(m->reads==reads);
}
int main(void) {
    const uint32_t ttb=0x8c145000u,slot=0x8c180000u,block=0x8c190000u;
    const uint32_t vas[]={KUI_ARMADA_CE_HEAP_NOMINAL,KUI_ARMADA_CE_HEAP_REAL,
        KUI_ARMADA_CE_SOUND_RAM_NOMINAL,KUI_ARMADA_CE_SOUND_RAM_REAL};
    unsigned cases=0;
    for(unsigned i=0;i<4;i++) for(unsigned aliases=0;aliases<8;aliases++) {
        struct memory m=fixture(vas[i],ttb|((aliases&1u)?0x20000000u:0u),
            slot|((aliases&2u)?0x20000000u:0u),block|((aliases&4u)?0x20000000u:0u),0x0c1a0079u|(vas[i]&0x3000u));
        if(aliases) {
            enum kui_armada_ce_ram_bind_status status=(aliases&1u)?KUI_ARMADA_CE_RAM_BIND_TTB:
                (aliases&2u)?KUI_ARMADA_CE_RAM_BIND_SLOT:KUI_ARMADA_CE_RAM_BIND_BLOCK;
            refused(&m,ttb|((aliases&1u)?0x20000000u:0u),vas[i],status,
                (aliases&1u)?0u:(aliases&2u)?1u:2u);++cases;continue;
        }
        struct kui_armada_ce_ram_binding out;
        assert(kui_armada_ce_ram_bind(ttb|((aliases&1u)?0x20000000u:0u),vas[i],0x105u,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_OK);
        assert(out.physical_address==(0x8c1a0000u|(vas[i]&0x3fffu)) && out.value==0x000f1234u);
        assert(out.page_word==(0x0c1a0079u|(vas[i]&0x3000u)) && m.reads==7);++cases;
    }
    /* Same nominal RVA in two slots must resolve independently. */
    struct memory separate=fixture(vas[0],ttb,slot,block,0x0c1a1079u);
    struct memory other=fixture(vas[1],ttb,slot+0x1000u,block+0x1000u,0x0c1b1079u);
    for(unsigned i=0;i<other.count;i++) put(&separate,other.cells[i].address,other.cells[i].value);
    struct kui_armada_ce_ram_binding a,b;
    assert(kui_armada_ce_ram_bind(ttb,vas[0],0x105u,read_word,&separate,&a)==KUI_ARMADA_CE_RAM_BIND_OK);
    assert(kui_armada_ce_ram_bind(ttb,vas[1],0x105u,read_word,&separate,&b)==KUI_ARMADA_CE_RAM_BIND_OK);
    assert(a.physical_address!=b.physical_address);++cases;
    for(unsigned stage=1;stage<=7;stage++) {
        struct memory m=fixture(vas[0],ttb,slot,block,0x0c1a1079u);m.fail_at=stage;
        refused(&m,ttb,vas[0],KUI_ARMADA_CE_RAM_BIND_READ,stage);++cases;
    }
    for(unsigned stage=5;stage<=7;stage++) {
        struct memory m=fixture(vas[0],ttb,slot,block,0x0c1a1079u);m.change_at=stage;
        refused(&m,ttb,vas[0],KUI_ARMADA_CE_RAM_BIND_CHANGED,stage);++cases;
    }
    const uint32_t unsafe[]={0u,0x8c000000u,0x8d000000u,0xa0800000u,0xff000008u,0x01d55000u,0x8c180001u};
    for(unsigned i=0;i<sizeof(unsafe)/sizeof(unsafe[0]);i++) {
        struct memory m={0};refused(&m,unsafe[i],vas[0],KUI_ARMADA_CE_RAM_BIND_TTB,0);++cases;
        m=(struct memory){0};put(&m,ttb,unsafe[i]);
        refused(&m,ttb,vas[0],KUI_ARMADA_CE_RAM_BIND_SLOT,1);++cases;
        m=(struct memory){0};put(&m,ttb,slot);put(&m,slot+4u*((vas[0]>>16)&511u),unsafe[i]);
        refused(&m,ttb,vas[0],KUI_ARMADA_CE_RAM_BIND_BLOCK,2);++cases;
    }
    /* Never dereference a mapped G2 endpoint or a firmware/resident alias. */
    const uint32_t bad_pages[]={0x00800071u,0x0c000071u,0x0d000071u,0x1fd80071u,0x0c1a0070u};
    for(unsigned i=0;i<sizeof(bad_pages)/sizeof(bad_pages[0]);i++) {
        struct memory m=fixture(vas[0],ttb,slot,block,bad_pages[i]);
        refused(&m,ttb,vas[0],i==4?KUI_ARMADA_CE_RAM_BIND_PAGE:KUI_ARMADA_CE_RAM_BIND_DESTINATION,3);++cases;
    }
    struct memory m={0};refused(&m,ttb,vas[0]|1u,KUI_ARMADA_CE_RAM_BIND_ARGUMENT,0);++cases;
    m=(struct memory){0};refused(&m,ttb,0x8c145000u,KUI_ARMADA_CE_RAM_BIND_ARGUMENT,0);++cases;
    assert(kui_armada_ce_ram_bind(ttb,vas[0],0x105u,read_word,&m,NULL)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);++cases;
    /* Cache mismatches publish only a stable mapping; the holder read must
     * never occur. Removing the destination cell makes an accidental read
     * fail, and the exact six-read count distinguishes the safe path. */
    const uint32_t cache_modes[]={0u,0x104u,0x125u,0x185u,0x1a5u};
    for(unsigned i=0;i<sizeof(cache_modes)/sizeof(cache_modes[0]);i++) {
        m=fixture(vas[0],ttb,slot,block,0x0c1a1079u);--m.count;
        struct kui_armada_ce_ram_binding out;
        assert(kui_armada_ce_ram_bind(ttb,vas[0],cache_modes[i],read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_CACHE);
        assert(out.physical_address==0x8c1a10acu && out.page_word==0x0c1a1079u && out.value==0 && m.reads==6);++cases;
    }
    const uint32_t cache_pages[]={0x0c1a0079u,0x0c1a2079u,0x0c1a3079u,0x0c1a1071u};
    for(unsigned i=0;i<sizeof(cache_pages)/sizeof(cache_pages[0]);i++) {
        m=fixture(vas[0],ttb,slot,block,cache_pages[i]);--m.count;
        struct kui_armada_ce_ram_binding out;
        assert(kui_armada_ce_ram_bind(ttb,vas[0],0x105u,read_word,&m,&out)==KUI_ARMADA_CE_RAM_BIND_CACHE);
        assert(out.physical_address==((cache_pages[i]&0xfffff000u)|0x800000acu));
        assert(out.page_word==cache_pages[i] && out.value==0 && m.reads==6);++cases;
    }
    for(unsigned stage=1;stage<=6;stage++) {
        m=fixture(vas[0],ttb,slot,block,0x0c1a1071u);m.fail_at=stage;
        refused(&m,ttb,vas[0],KUI_ARMADA_CE_RAM_BIND_READ,stage);++cases;
    }
    for(unsigned stage=4;stage<=6;stage++) {
        m=fixture(vas[0],ttb,slot,block,0x0c1a1071u);m.change_at=stage;
        refused(&m,ttb,vas[0],KUI_ARMADA_CE_RAM_BIND_CHANGED,stage);++cases;
    }
    puts("Armada CE RAM binding tests passed");printf("%u cases; no invalid address reached a read callback.\n",cases);
    return 0;
}
