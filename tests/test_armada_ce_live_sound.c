/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_live_sound.h"
#include "kui/armada_ce_cache_word.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* This fixture contains independently authored words, not game/kernel/DLL
 * instructions. CRC guards are met with a linear four-byte correction. */
#define TEST_CELLS 4096u
#define TEST_PAGES 128u
#define TEST_READS 4096u
#define TEST_TTB UINT32_C(0x8c1458a0)
#define TEST_CCR UINT32_C(0x105)
struct test_cell { uint32_t address, value; };
struct test_page { uint32_t virtual_page, physical_page; };
struct test_memory {
    struct test_cell cells[TEST_CELLS];
    struct test_page pages[TEST_PAGES];
    uint32_t addresses[TEST_READS];
    unsigned count, page_count, block_count, reads, fail_at;
    uint32_t change_address, change_mask;
    unsigned change_on, change_hits;
    bool change_persistent;
    uint32_t cache_va,cache_p1,cache_change_address,cache_change_mask;
    unsigned cache_calls,cache_reads,cache_fail_at,cache_change_on,cache_change_hits;
};
static void put(struct test_memory *m,uint32_t address,uint32_t value) {
    for(unsigned i=0;i<m->count;i++) if(m->cells[i].address==address) {
        m->cells[i].value=value; return;
    }
    assert(m->count<TEST_CELLS);
    m->cells[m->count++]=(struct test_cell){address,value};
}
static bool lookup(const struct test_memory *m,uint32_t address,uint32_t *value) {
    for(unsigned i=0;i<m->count;i++) if(m->cells[i].address==address) {
        *value=m->cells[i].value; return true;
    }
    return false;
}
static uint32_t get(const struct test_memory *m,uint32_t address) {
    uint32_t value; assert(lookup(m,address,&value)); return value;
}
static bool read_word(void *context,uint32_t address,uint32_t *value) {
    struct test_memory *m=context;
    uint32_t checked;
    /* Any P2 alias, unaligned word, reserved RAM or G2/MMIO access fails
     * the test before the callback even looks for fixture data. */
    assert(kui_armada_ce_ram_table_p1(address,&checked) && checked==address);
    assert(m->reads<TEST_READS);
    m->addresses[m->reads++]=address;
    if(m->reads==m->fail_at) return false;
    if(!lookup(m,address,value)) return false;
    if(address==m->change_address) {
        ++m->change_hits;
        if(m->change_hits==m->change_on ||
           (m->change_persistent && m->change_hits>m->change_on)) *value^=m->change_mask;
    }
    return true;
}
static uint32_t physical(struct test_memory *m,uint32_t va) {
    uint32_t page=va&UINT32_C(0xfffff000);
    for(unsigned i=0;i<m->page_count;i++) if(m->pages[i].virtual_page==page)
        return m->pages[i].physical_page|(va&0xfffu);
    assert(va<0x80000000u && !(va&3u) && m->page_count<TEST_PAGES);
    uint32_t slot=0x8c200000u+(va>>25)*0x1000u;
    uint32_t block_at=slot+4u*((va>>16)&511u),block;
    put(m,TEST_TTB+4u*(va>>25),slot);
    if(!lookup(m,block_at,&block)) {
        block=0x8c400000u+0x100u*m->block_count++;
        put(m,block_at,block);
    }
    uint32_t p1=0x8c800000u+0x4000u*m->page_count+(va&0x3000u);
    m->pages[m->page_count++]=(struct test_page){page,p1};
    put(m,block+12u+4u*((va>>12)&15u),(p1&0x1ffff000u)|0x79u);
    put(m,p1,0); /* The final mapping witness samples the page's first word. */
    return p1|(va&0xfffu);
}
static void virtual_put(struct test_memory *m,uint32_t va,uint32_t value) {
    put(m,physical(m,va),value);
}
static uint32_t virtual_get(struct test_memory *m,uint32_t va) {
    return get(m,physical(m,va));
}
static uint32_t page_entry(const struct test_memory *m,uint32_t va) {
    uint32_t slot=get(m,TEST_TTB+4u*(va>>25));
    uint32_t block=get(m,slot+4u*((va>>16)&511u));
    return block+12u+4u*((va>>12)&15u);
}
static uint32_t test_crc32(const uint8_t *bytes,unsigned size) {
    uint32_t crc=UINT32_C(0xffffffff);
    for(unsigned i=0;i<size;i++) {
        crc^=bytes[i];
        for(unsigned j=0;j<8;j++)
            crc=(crc>>1)^((crc&1u)?UINT32_C(0xedb88320):0u);
    }
    return ~crc;
}
static void crc_guard(struct test_memory *m,uint32_t va,unsigned size,uint32_t target) {
    uint8_t bytes[32];
    uint32_t basis[32]={0},combination[32]={0};
    assert(size>=4 && size<=sizeof(bytes) && !(size&3u));
    for(unsigned i=0;i<size;i++) bytes[i]=(uint8_t)(0x51u+13u*i);
    memset(bytes+size-4u,0,4);
    uint32_t baseline=test_crc32(bytes,size);
    for(unsigned bit=0;bit<32;bit++) {
        bytes[size-4u+bit/8u]^=(uint8_t)(1u<<(bit&7u));
        uint32_t column=test_crc32(bytes,size)^baseline,mask=UINT32_C(1)<<bit;
        bytes[size-4u+bit/8u]^=(uint8_t)(1u<<(bit&7u));
        for(int row=31;row>=0;row--) if(column&(UINT32_C(1)<<(unsigned)row)) {
            if(basis[row]) { column^=basis[row]; mask^=combination[row]; }
            else { basis[row]=column; combination[row]=mask; break; }
        }
    }
    uint32_t remainder=baseline^target,patch=0;
    for(int row=31;row>=0;row--) if(remainder&(UINT32_C(1)<<(unsigned)row)) {
        assert(basis[row]); remainder^=basis[row]; patch^=combination[row];
    }
    assert(!remainder);
    for(unsigned i=0;i<4;i++) bytes[size-4u+i]=(uint8_t)(patch>>(8u*i));
    assert(test_crc32(bytes,size)==target);
    for(unsigned i=0;i<size;i+=4)
        virtual_put(m,va+i,(uint32_t)bytes[i]|((uint32_t)bytes[i+1]<<8)|
            ((uint32_t)bytes[i+2]<<16)|((uint32_t)bytes[i+3]<<24));
}

#define TEST_DS_NODE UINT32_C(0x8c300100)
#define TEST_SN_NODE UINT32_C(0x8c300200)
#define TEST_CORE_NODE UINT32_C(0x8c300300)
#define TEST_DS_BASE UINT32_C(0x03cd0000)
#define TEST_SN_BASE UINT32_C(0x03d50000)
#define TEST_CORE_BASE UINT32_C(0x03d10000)
#define TEST_DS_SECTIONS (TEST_DS_BASE+UINT32_C(0x846c))
#define TEST_SN_SECTIONS (TEST_SN_BASE+UINT32_C(0x6198))
#define TEST_CORE_SECTIONS (TEST_CORE_BASE+UINT32_C(0x2e170))
#define TEST_DEVICE UINT32_C(0x00123400)
static uint32_t owner_slot(unsigned owner) { return (owner+1u)<<25; }
static uint32_t owner_process(unsigned owner) {
    return KUI_ARMADA_CE_LIVE_PROCESS_BASE+KUI_ARMADA_CE_LIVE_PROCESS_STRIDE*owner;
}
static uint32_t ds_alias(unsigned owner,uint32_t offset) {
    return owner_slot(owner)|((TEST_DS_BASE&0x01ffffffu)+offset);
}
static uint32_t core_alias(unsigned owner,uint32_t offset) {
    return owner_slot(owner)|((TEST_CORE_BASE&0x01ffffffu)+offset);
}
/* Independent model of the reviewed NK loader: append the NUL-terminated
 * ASCII basename to .data, align to four bytes, then append all 28-byte
 * live section records. Original PE/ROM metadata is therefore not the final
 * loaded .data span, and the fourth disk .reloc record remains in the table
 * after its storage is discarded. */
static uint32_t loaded_data_bytes(uint32_t original,const char *basename,unsigned count) {
    return ((original+(uint32_t)strlen(basename)+1u+3u)&~3u)+28u*count;
}
static void sections(struct test_memory *m,uint32_t table,uint32_t base,bool dsound) {
    static const uint32_t ds_rva[3]={0x1000u,0x8000u,0x9000u};
    static const uint32_t sn_rva[3]={0x1000u,0x5000u,0x7000u};
    static const uint32_t ds_size[3]={0x61a6u,0x460u,0x550u};
    static const uint32_t sn_size[3]={0x3c25u,0x118cu,0x3d0u};
    static const uint32_t flags[3]={0x60000020u,0xc0002040u,0x40002040u};
    for(unsigned i=0;i<3u;i++) {
        uint32_t rva=dsound?ds_rva[i]:sn_rva[i],size=dsound?ds_size[i]:sn_size[i];
        if(i==1u) size=loaded_data_bytes(size,dsound?"dsound.dll":"sndcore.dll",3u);
        virtual_put(m,table+28u*i,size); virtual_put(m,table+28u*i+4u,rva);
        virtual_put(m,table+28u*i+8u,base+rva); virtual_put(m,table+28u*i+16u,flags[i]);
    }
}
static void module(struct test_memory *m,uint32_t node,uint32_t next,uint32_t base,
                   uint32_t table,uint32_t toc,unsigned owner,bool dsound) {
    put(m,node,node); put(m,node+4u,next); put(m,node+12u,UINT32_C(1)<<owner);
    for(unsigned i=0;i<16u;i++) put(m,node+16u+4u*i,0);
    put(m,node+16u+4u*(owner/2u),UINT32_C(2)<<(16u*(owner&1u)));
    put(m,node+80u,base); put(m,node+84u,0x40000u);
    put(m,node+96u,toc); put(m,node+100u,3u);
    put(m,node+112u,3u); put(m,node+124u,dsound?0xb000u:0x9000u);
    put(m,node+176u,table);
    sections(m,table,base,dsound);
}
static void core_module(struct test_memory *m,unsigned owner) {
    static const uint32_t rva[4]={0x1000u,0x2e000u,0x2f000u,0x31000u};
    static const uint32_t size[4]={0x2ccc4u,0x164u,0x1848u,0x218u};
    static const uint32_t flags[4]={0x60000020u,0xc0300040u,0x40002040u,0x40000040u};
    module(m,TEST_CORE_NODE,0,TEST_CORE_BASE,TEST_CORE_SECTIONS,0x8c141494u,owner,false);
    put(m,TEST_CORE_NODE+112u,4u); put(m,TEST_CORE_NODE+124u,0x34000u);
    for(unsigned i=0;i<4u;i++) {
        virtual_put(m,TEST_CORE_SECTIONS+28u*i,i==1u?loaded_data_bytes(size[i],"coredll.dll",4u):size[i]);
        virtual_put(m,TEST_CORE_SECTIONS+28u*i+4u,rva[i]);
        virtual_put(m,TEST_CORE_SECTIONS+28u*i+8u,TEST_CORE_BASE+rva[i]);
        virtual_put(m,TEST_CORE_SECTIONS+28u*i+16u,flags[i]);
    }
}
static void game_identity(struct test_memory *m,unsigned owner) {
    uint32_t slot=owner_slot(owner);
    crc_guard(m,slot|0x995d4u,32u,0x9baf8d11u);
    crc_guard(m,slot|0xce900u,12u,0x46696861u);
    crc_guard(m,slot|0x996a0u,28u,0xb374b05au);
}
static void fixture(struct test_memory *m,unsigned owner) {
    memset(m,0,sizeof(*m));
    put(m,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,owner_process(owner));
    put(m,0x8c14d430u,0x8c300000u); put(m,0x8c14d434u,0x8c308000u);
    put(m,KUI_ARMADA_CE_LIVE_MODULE_HEAD,TEST_DS_NODE);
    module(m,TEST_DS_NODE,TEST_SN_NODE,TEST_DS_BASE,TEST_DS_SECTIONS,
           KUI_ARMADA_CE_LIVE_DSOUND_TOC,owner,true);
    module(m,TEST_SN_NODE,TEST_CORE_NODE,TEST_SN_BASE,TEST_SN_SECTIONS,
           KUI_ARMADA_CE_LIVE_SNDCORE_TOC,owner,false);
    core_module(m,owner);
    for(unsigned i=0;i<32u;i++) {
        put(m,owner_process(i),0x77aa0000u|i);
        put(m,owner_process(i)+12u,i==owner?owner_slot(i):0);
    }
    game_identity(m,owner);
    uint32_t slot=owner_slot(owner),device=slot|TEST_DEVICE;
    virtual_put(m,slot|0xf7180u,TEST_DEVICE);
    virtual_put(m,ds_alias(owner,0x8110u),device);
    virtual_put(m,slot|0xf21a8u,TEST_DS_BASE+0x15ecu);
    virtual_put(m,device,(TEST_DS_BASE&0x01ffffffu)+0x116cu);
    virtual_put(m,device+8u,3u);
    virtual_put(m,ds_alias(owner,0x116cu)+4u,TEST_DS_BASE+0x1994u);
    virtual_put(m,ds_alias(owner,0x116cu)+8u,TEST_DS_BASE+0x199cu);
    virtual_put(m,ds_alias(owner,0x116cu)+12u,TEST_DS_BASE+0x19dcu);
    virtual_put(m,ds_alias(owner,0x116cu)+24u,TEST_DS_BASE+0x1bccu);
    virtual_put(m,ds_alias(owner,0x801cu),TEST_CORE_BASE+0x8ea0u);
    virtual_put(m,ds_alias(owner,0x8020u),TEST_CORE_BASE+0x8efcu);
    crc_guard(m,core_alias(owner,0x8ea0u),32u,0xeea467ebu);
    crc_guard(m,core_alias(owner,0x8efcu),32u,0x01916477u);
    crc_guard(m,TEST_CORE_BASE+0x8ea0u,32u,0xeea467ebu);
    crc_guard(m,TEST_CORE_BASE+0x8efcu,32u,0x01916477u);
    static const uint32_t secondary_offsets[8]={8u,12u,44u,76u,48u,72u,36u,16u};
    static const uint32_t secondary_targets[8]={0x2910u,0x29fcu,0x2cbcu,0x39acu,
                                               0x2e54u,0x3898u,0x2c2cu,0x2a44u};
    for(unsigned i=0;i<8u;i++) virtual_put(m,ds_alias(owner,0x11ccu)+secondary_offsets[i],
                                          TEST_DS_BASE+secondary_targets[i]);
}
static struct kui_armada_ce_live_workspace bind_work;
static enum kui_armada_ce_live_status bind(struct test_memory *m,
                                         struct kui_armada_ce_live_sound *out) {
    enum kui_armada_ce_live_status status=kui_armada_ce_live_sound_bind(
        TEST_TTB,TEST_CCR,read_word,m,&bind_work,out);
    assert(out->reads==m->reads && m->reads<=KUI_ARMADA_CE_LIVE_READ_LIMIT);
    if(status!=KUI_ARMADA_CE_LIVE_OK) {
        struct kui_armada_ce_live_sound empty={0}; empty.reads=out->reads;
        assert(!memcmp(out,&empty,sizeof(empty)));
    }
    return status;
}
static void expect(struct test_memory *m,enum kui_armada_ce_live_status expected) {
    struct kui_armada_ce_live_sound out;
    enum kui_armada_ce_live_status actual=bind(m,&out);
    if(actual!=expected) fprintf(stderr,"expected status %u, got %u after %u reads\n",
                               (unsigned)expected,(unsigned)actual,m->reads);
    assert(actual==expected);
}
/* Independently generated disk-backed metadata and CRC-correct synthetic
 * instruction witnesses. No original DLL instruction bytes are included. */
static void disk_sound(struct test_memory *m,bool dsound,bool discarded) {
    uint32_t node=dsound?TEST_DS_NODE:TEST_SN_NODE;
    uint32_t table=dsound?TEST_DS_SECTIONS:TEST_SN_SECTIONS;
    uint32_t base=dsound?TEST_DS_BASE:TEST_SN_BASE;
    put(m,node+100u,2u);put(m,node+96u,dsound?0x00123456u:0x00654321u);
    put(m,node+112u,4u);
    virtual_put(m,table+28u,loaded_data_bytes(dsound?0x460u:0x118cu,
                                    dsound?"dsound.dll":"sndcore.dll",4u));
    virtual_put(m,table+28u+16u,0xc0000040u);
    virtual_put(m,table+56u+16u,0x40000040u);
    uint32_t reloc_rva=dsound?0xa000u:0x8000u;
    virtual_put(m,table+84u,dsound?0x764u:0x4b4u);
    virtual_put(m,table+88u,reloc_rva);virtual_put(m,table+92u,discarded?0u:base+reloc_rva);
    virtual_put(m,table+100u,0x42000040u);
    static const uint32_t offsets[2][3]={{0x15ecu,0x19dcu,0x2e54u},{0x10d8u,0x11c8u,0x12b8u}};
    static const uint32_t crc[2][3]={{0xeb34aa87u,0x34718dd9u,0x3453b46fu},{0x5ba8b52bu,0x2718008fu,0x834f5156u}};
    unsigned profile=dsound?0u:1u;
    for(unsigned i=0;i<3u;i++) crc_guard(m,base+offsets[profile][i],32u,crc[profile][i]);
}
static void unrelated_disk_nodes(struct test_memory *m,unsigned count) {
    if(!count) return;
    put(m,KUI_ARMADA_CE_LIVE_MODULE_HEAD,0x8c300400u);
    for(unsigned i=0;i<count;i++) {
        uint32_t node=0x8c300400u+0x100u*i;
        put(m,node,node);put(m,node+4u,i+1u==count?TEST_DS_NODE:node+0x100u);
        put(m,node+96u,0x123456u+i);put(m,node+100u,2u);
        put(m,node+124u,0x5000u);put(m,node+112u,3u);
    }
}
/* The regular callback above remains P1-only. This optional simulation uses
 * the production pure cache decision algorithm at a separate reviewed
 * boundary; only its exact tag/data-array and P2 main-RAM addresses exist. */
static bool cache_sample(void *context,uint32_t address,uint32_t *value) {
    struct test_memory *m=context;
    uint32_t canonical;
    assert(!(address&3u));
    if(address>=0xf4000000u && address<=0xf4003fe0u) {
        assert(!(address&31u));
        assert(lookup(m,address,value));
    } else if(address>=0xf5000000u && address<=0xf5003ffcu) {
        assert(lookup(m,address,value));
    } else {
        assert(kui_armada_ce_ram_p1(address,&canonical) &&
               (address&0xe0000000u)==0xa0000000u &&
               (canonical&0xfffff000u)==m->cache_p1);
        assert(lookup(m,canonical,value));
    }
    ++m->cache_reads;
    if(m->cache_reads==m->cache_fail_at) return false;
    if(address==m->cache_change_address &&
       ++m->cache_change_hits==m->cache_change_on) *value^=m->cache_change_mask;
    return true;
}
static enum kui_armada_ce_ram_bind_status coherent_word(
    void *context,uint32_t va,uint32_t p1,uint32_t page,uint32_t ccr,uint32_t *out) {
    struct test_memory *m=context;
    assert((va&0xfffff000u)==m->cache_va && (p1&0xfffff000u)==m->cache_p1);
    unsigned before=m->cache_reads;
    ++m->cache_calls;
    enum kui_armada_ce_ram_bind_status status=kui_armada_ce_cache_word(
        va,p1,page,ccr,cache_sample,m,out);
    assert(m->cache_reads-before<=10u);
    return status;
}
static enum kui_armada_ce_live_status bind_coherent(
    struct test_memory *m,struct kui_armada_ce_live_sound *out) {
    enum kui_armada_ce_live_status status=kui_armada_ce_live_sound_bind_with_reader(
        TEST_TTB,TEST_CCR,read_word,m,coherent_word,&bind_work,out);
    assert(out->reads==m->reads && m->reads<=KUI_ARMADA_CE_LIVE_READ_LIMIT);
    /* Every fallback begins with three ordinary budgeted table reads.
     * Each optional call has a separately bounded ten array/P2 reads. */
    assert(m->cache_calls<=m->reads/3u && m->cache_reads<=m->cache_calls*10u);
    if(status!=KUI_ARMADA_CE_LIVE_OK) {
        struct kui_armada_ce_live_sound empty={0};empty.reads=out->reads;
        assert(!memcmp(out,&empty,sizeof(empty)));
    }
    return status;
}
static void cache_section_fixture(struct test_memory *m,unsigned owner) {
    fixture(m,owner);disk_sound(m,true,true);disk_sound(m,false,true);
    const uint32_t table=0x03cd846cu;
    /* The console's refused VA now names a synthetic four-row disk module
     * section table. Its physical page has a different index color; dirty
     * target cache lines hold current metadata while main RAM is stale. */
    for(unsigned row=0;row<4u;row++) for(unsigned col=0;col<4u;col++) {
        static const uint32_t offsets[4]={0u,4u,8u,16u};
        uint32_t offset=28u*row+offsets[col];
        virtual_put(m,table+offset,virtual_get(m,TEST_DS_SECTIONS+offset));
    }
    put(m,TEST_DS_NODE+176u,table);
    uint32_t old_page=physical(m,table)&0xfffff000u,new_page=old_page^0x1000u;
    for(unsigned i=0;i<m->page_count;i++) if(m->pages[i].virtual_page==(table&0xfffff000u))
        m->pages[i].physical_page=new_page;
    uint32_t pte=page_entry(m,table);
    put(m,pte,(new_page&0x1ffff000u)|0x79u);
    m->cache_va=table&0xfffff000u;m->cache_p1=new_page;
    unsigned count=m->count;
    for(unsigned i=0;i<count;i++) if((m->cells[i].address&0xfffff000u)==old_page) {
        uint32_t address=new_page|(m->cells[i].address&0xfffu),value=m->cells[i].value;
        uint32_t index=address&0xfe0u,color=(table>>12)&3u;
        for(unsigned alias=0;alias<4u;alias++)
            put(m,0xf4000000u|index|(alias<<12),alias==color?(address&0x1ffffc00u)|3u:0u);
        put(m,0xf5000000u|index|(color<<12)|(address&0x1cu),value);
        put(m,address,value^4u);
    }
}
int main(void) {
    static struct test_memory m,before;
    struct kui_armada_ce_live_sound out;
    unsigned cases=0;
    unsigned disk_reads=0;
    assert(loaded_data_bytes(0x460u,"dsound.dll",3u)==0x4c0u &&
           loaded_data_bytes(0x460u,"dsound.dll",4u)==0x4dcu &&
           loaded_data_bytes(0x118cu,"sndcore.dll",3u)==0x11ecu &&
           loaded_data_bytes(0x118cu,"sndcore.dll",4u)==0x1208u &&
           loaded_data_bytes(0x164u,"coredll.dll",4u)==0x1e0u);++cases;
    /* Reject unfinished raw/name-only spans and a wrong embedded table
     * length for each ROM/disk sound form and COREDLL. Repeated final .data
     * metadata is part of the publication transaction, never a loose range. */
    for(unsigned profile=0;profile<3u;profile++) for(unsigned disk=0;disk<2u;disk++) {
        if(profile==2u && disk) continue;
        const uint32_t table=profile==0u?TEST_DS_SECTIONS:
            profile==1u?TEST_SN_SECTIONS:TEST_CORE_SECTIONS;
        const uint32_t node=profile==0u?TEST_DS_NODE:
            profile==1u?TEST_SN_NODE:TEST_CORE_NODE;
        const uint32_t original=profile==0u?0x460u:profile==1u?0x118cu:0x164u;
        const char *name=profile==0u?"dsound.dll":profile==1u?"sndcore.dll":"coredll.dll";
        const unsigned count=profile==2u || disk?4u:3u;
        const uint32_t final_size=loaded_data_bytes(original,name,count);
        const uint32_t invalid[3]={original,final_size-28u*count,final_size+28u};
        for(unsigned form=0;form<3u;form++) {
            fixture(&m,2u);if(disk) disk_sound(&m,profile==0u,true);
            virtual_put(&m,table+28u,invalid[form]);expect(&m,KUI_ARMADA_CE_LIVE_MODULE);++cases;
        }
        const uint32_t wrong_table[5]={table-4u,table+4u,
            table^0x02000000u,0x8c310000u,0xac310000u};
        for(unsigned form=0;form<5u;form++) {
            fixture(&m,2u);if(disk) disk_sound(&m,profile==0u,true);
            put(&m,node+176u,wrong_table[form]);expect(&m,KUI_ARMADA_CE_LIVE_MODULE);++cases;
        }
        fixture(&m,2u);if(disk) disk_sound(&m,profile==0u,true);
        m.change_address=physical(&m,table+28u);m.change_on=disk?3u:2u;m.change_mask=4u;
        expect(&m,KUI_ARMADA_CE_LIVE_CHANGED);++cases;
    }
    /* Regression from the console: the strict observer publishes CACHE=9
     * at VA 03cd846c without reading that holder. The isolated coherent
     * fallback obtains current dirty cache data, not stale physical RAM. */
    cache_section_fixture(&m,2u);
    expect(&m,KUI_ARMADA_CE_LIVE_CACHE);
    assert(bind_work.fault[0]==KUI_ARMADA_CE_LIVE_FAULT_MAPPING &&
           bind_work.fault[1]==0x03cd846cu && bind_work.fault[2]==KUI_ARMADA_CE_RAM_BIND_CACHE);
    uint32_t strict_reads=m.reads;
    for(unsigned i=0;i<m.reads;i++) assert(m.addresses[i]!=physical(&m,0x03cd846cu));
    assert(!m.cache_calls && !m.cache_reads);++cases;
    cache_section_fixture(&m,2u);before=m;
    assert(bind_coherent(&m,&out)==KUI_ARMADA_CE_LIVE_OK);
    assert(out.owner_index==2u && out.device==(owner_slot(2u)|TEST_DEVICE));
    assert(m.count==before.count && !memcmp(m.cells,before.cells,sizeof(m.cells)));
    unsigned coherent_reads=m.reads,cache_reads=m.cache_reads,cache_calls=m.cache_calls;++cases;
    /* Rechecks after fallback reject a changed software page table; stale
     * metadata or unowned identity can never become admitted call targets. */
    cache_section_fixture(&m,2u);m.change_address=page_entry(&m,0x03cd846cu);
    m.change_on=2u;m.change_mask=0x4000u;
    assert(bind_coherent(&m,&out)==KUI_ARMADA_CE_LIVE_CHANGED && m.cache_calls==1u);++cases;
    cache_section_fixture(&m,2u);put(&m,TEST_DS_NODE+20u,0u);
    assert(bind_coherent(&m,&out)==KUI_ARMADA_CE_LIVE_OWNER);++cases;
    cache_section_fixture(&m,2u);
    virtual_put(&m,owner_slot(2u)|0x995d4u,virtual_get(&m,owner_slot(2u)|0x995d4u)^1u);
    assert(bind_coherent(&m,&out)==KUI_ARMADA_CE_LIVE_OWNER);++cases;
    cache_section_fixture(&m,2u);
    put(&m,0xf4000000u|(physical(&m,0x03cd846cu)&0xfe0u)|0x1000u,
        (physical(&m,0x03cd846cu)&0x1ffffc00u)|3u);
    assert(bind_coherent(&m,&out)==KUI_ARMADA_CE_LIVE_CACHE);++cases;
    /* Every new array/P2 failure and every original callback failure leaves
     * no partial object publication; each path stays within both budgets. */
    for(unsigned at=1;at<=cache_reads;at++) {
        cache_section_fixture(&m,2u);m.cache_fail_at=at;
        assert(bind_coherent(&m,&out)==KUI_ARMADA_CE_LIVE_READ && m.cache_reads==at);++cases;
    }
    for(unsigned at=1;at<=coherent_reads;at++) {
        cache_section_fixture(&m,2u);m.fail_at=at;
        assert(bind_coherent(&m,&out)==KUI_ARMADA_CE_LIVE_READ && m.reads==at);++cases;
    }
    /* Cache data and tags are repeated inside each optional read. A change
     * cannot sneak through the larger module identity/owner transaction. */
    for(unsigned what=0;what<5u;what++) {
        cache_section_fixture(&m,2u);
        uint32_t p1=physical(&m,0x03cd846cu),index=p1&0xfe0u;
        m.cache_change_address=what<4u?(0xf4000000u|index|(what<<12)):
            (0xf5000000u|index|(p1&0x1cu));
        m.cache_change_on=2u;m.cache_change_mask=1u;
        assert(bind_coherent(&m,&out)==KUI_ARMADA_CE_LIVE_CHANGED && m.cache_calls==1u);++cases;
    }
    /* A page-table change after a later callback also fails the public
     * RAM binder's repeated walk, rather than publishing sampled bytes. */
    cache_section_fixture(&m,2u);m.change_address=page_entry(&m,0x03cd846cu);
    m.change_on=3u;m.change_mask=0x4000u;m.change_persistent=true;
    struct kui_armada_ce_ram_binding mapped;
    assert(kui_armada_ce_ram_bind_with_reader(TEST_TTB,0x03cd846cu,TEST_CCR,
        read_word,coherent_word,&m,&mapped)==KUI_ARMADA_CE_RAM_BIND_OK);
    m.change_on=4u;
    assert(kui_armada_ce_ram_bind_with_reader(TEST_TTB,0x03cd846cu,TEST_CCR,
        read_word,coherent_word,&m,&mapped)==KUI_ARMADA_CE_RAM_BIND_CHANGED);++cases;
    /* Optional reads cannot bypass the original global table/RAM budget.
     * All 32 candidates have exact first two game guards; the last guard
     * is deliberately wrong for nonowners, consuming bounded work. */
    cache_section_fixture(&m,2u);
    put(&m,TEST_DS_NODE+12u,UINT32_MAX);put(&m,TEST_SN_NODE+12u,UINT32_MAX);
    put(&m,TEST_CORE_NODE+12u,UINT32_MAX);
    for(unsigned i=0;i<16u;i++) {
        put(&m,TEST_DS_NODE+16u+4u*i,0x00020002u);
        put(&m,TEST_SN_NODE+16u+4u*i,0x00020002u);
        put(&m,TEST_CORE_NODE+16u+4u*i,0x00020002u);
    }
    for(unsigned owner=0;owner<32u;owner++) {
        put(&m,owner_process(owner)+12u,owner_slot(owner));game_identity(&m,owner);
        if(owner!=2u) virtual_put(&m,owner_slot(owner)|0x996a0u,
            virtual_get(&m,owner_slot(owner)|0x996a0u)^1u);
    }
    assert(bind_coherent(&m,&out)==KUI_ARMADA_CE_LIVE_BUDGET &&
           m.reads==KUI_ARMADA_CE_LIVE_READ_LIMIT);++cases;
    printf("Console cache regression: strict CACHE=9 after %u reads at VA 03cd846c; coherent admission %u RAM reads + %u array/P2 reads in %u bounded calls.\n",
           strict_reads,coherent_reads,cache_reads,cache_calls);
    /* The old ROM-only recognizer walks this exact authored 16-node shape
     * and refuses with MODULE after 74 reads, matching the console report.
     * The corrected binder must recognize both sound DLLs and still leave
     * every word in the read-only fixture unchanged. */
    for(unsigned owner=0;owner<32u;owner++) for(unsigned forms=1u;forms<=3u;forms++) {
        fixture(&m,owner);
        if(forms&1u) disk_sound(&m,true,owner&1u);
        if(forms&2u) disk_sound(&m,false,owner&1u);
        unrelated_disk_nodes(&m,13u);before=m;
        assert(bind(&m,&out)==KUI_ARMADA_CE_LIVE_OK);
        assert(out.owner_process==owner_process(owner) && out.device==(owner_slot(owner)|TEST_DEVICE));
        assert(m.count==before.count && !memcmp(m.cells,before.cells,sizeof(m.cells)));
        if(m.reads>disk_reads) disk_reads=m.reads;
        ++cases;
    }
    /* Every observed disk admission failure remains fail-closed. */
    fixture(&m,3u);disk_sound(&m,true,true);disk_sound(&m,false,true);
    unrelated_disk_nodes(&m,13u);assert(bind(&m,&out)==KUI_ARMADA_CE_LIVE_OK);
    unsigned disk_success_reads=m.reads;
    for(unsigned at=1;at<=disk_success_reads;at++) {
        fixture(&m,3u);disk_sound(&m,true,true);disk_sound(&m,false,true);
        unrelated_disk_nodes(&m,13u);m.fail_at=at;
        expect(&m,KUI_ARMADA_CE_LIVE_READ);assert(m.reads==at);++cases;
    }
    for(unsigned profile=0;profile<2u;profile++) for(unsigned range=0;range<3u;range++) {
        static const uint32_t offsets[2][3]={{0x15ecu,0x19dcu,0x2e54u},{0x10d8u,0x11c8u,0x12b8u}};
        uint32_t base=profile?TEST_SN_BASE:TEST_DS_BASE;
        fixture(&m,3u);disk_sound(&m,true,true);disk_sound(&m,false,true);
        virtual_put(&m,base+offsets[profile][range],virtual_get(&m,base+offsets[profile][range])^1u);
        expect(&m,KUI_ARMADA_CE_LIVE_MODULE);++cases;
        fixture(&m,3u);disk_sound(&m,true,true);disk_sound(&m,false,true);
        m.change_address=physical(&m,base+offsets[profile][range]);m.change_on=2u;m.change_mask=1u;
        expect(&m,KUI_ARMADA_CE_LIVE_CHANGED);++cases;
    }
    /* Only relocation storage may be discarded, and its metadata stays
     * exact. Zero code/data/exception addresses never become an audio owner. */
    for(unsigned profile=0;profile<2u;profile++) for(unsigned row=0;row<4u;row++) for(unsigned field=0;field<4u;field++) {
        fixture(&m,3u);disk_sound(&m,true,true);disk_sound(&m,false,true);
        static const uint32_t fields[4]={0u,4u,8u,16u};
        uint32_t table=profile?TEST_SN_SECTIONS:TEST_DS_SECTIONS;
        uint32_t at=table+row*28u+fields[field];
        virtual_put(&m,at,field==2u && row<3u?0u:virtual_get(&m,at)^4u);
        expect(&m,KUI_ARMADA_CE_LIVE_MODULE);++cases;
    }
    /* A shape collision with an unrelated DLL is not an audio owner. The
     * real sound DLL that follows is still found; an exact duplicate fails. */
    for(unsigned matching=0;matching<2u;matching++) {
        fixture(&m,3u);disk_sound(&m,true,true);disk_sound(&m,false,true);
        const uint32_t node=0x8c300400u,table=0x03cc846cu;
        for(unsigned at=0;at<180u;at+=4u) {uint32_t value;if(lookup(&m,TEST_DS_NODE+at,&value)) put(&m,node+at,value);}
        put(&m,node,node);put(&m,node+4u,TEST_DS_NODE);put(&m,node+80u,0x03cc0000u);put(&m,node+176u,table);
        for(unsigned row=0;row<4u;row++) for(unsigned field=0;field<4u;field++) {
            static const uint32_t offsets[4]={0u,4u,8u,16u};
            uint32_t value=virtual_get(&m,TEST_DS_SECTIONS+28u*row+offsets[field]);
            if(field==2u && value) value=0x03cc0000u+(value-TEST_DS_BASE);
            virtual_put(&m,table+28u*row+offsets[field],value);
        }
        crc_guard(&m,0x03cc15ecu,32u,matching?0xeb34aa87u:0x12345678u);
        crc_guard(&m,0x03cc19dcu,32u,0x34718dd9u);crc_guard(&m,0x03cc2e54u,32u,0x3453b46fu);
        put(&m,KUI_ARMADA_CE_LIVE_MODULE_HEAD,node);
        if(matching) expect(&m,KUI_ARMADA_CE_LIVE_MODULE);
        else assert(bind(&m,&out)==KUI_ARMADA_CE_LIVE_OK);
        ++cases;
    }
    /* Module failure witnesses contain only already-read metadata. */
    fixture(&m,3u);put(&m,TEST_DS_NODE+80u,0x01d40000u);
    expect(&m,KUI_ARMADA_CE_LIVE_MODULE);
    assert(bind_work.fault[0]==KUI_ARMADA_CE_LIVE_FAULT_BASE && bind_work.fault[1]==TEST_DS_NODE);
    assert(bind_work.fault[2]==0x01d40000u && bind_work.fault[3]==0x02000000u);++cases;
    for(unsigned owner=0;owner<32u;owner++) {
        fixture(&m,owner); before=m;
        assert(bind(&m,&out)==KUI_ARMADA_CE_LIVE_OK);
        assert(out.current_process==owner_process(owner));
        assert(out.owner_process==owner_process(owner) && out.owner_index==owner);
        assert(out.owner_slot==owner_slot(owner));
        assert(out.dsound_module==TEST_DS_NODE && out.sndcore_module==TEST_SN_NODE);
        assert(out.dsound_base==TEST_DS_BASE && out.sndcore_base==TEST_SN_BASE);
        assert(out.coredll_module==TEST_CORE_NODE && out.coredll_base==TEST_CORE_BASE);
        assert(out.device==(owner_slot(owner)|TEST_DEVICE));
        assert(out.vtable==ds_alias(owner,0x116cu));
        assert(out.direct_sound_create==TEST_DS_BASE+0x15ecu);
        assert(out.create_sound_buffer==TEST_DS_BASE+0x19dcu);
        assert(out.release==TEST_DS_BASE+0x199cu && out.set_cooperative==TEST_DS_BASE+0x1bccu);
        assert(out.refcount==3u);
        assert(out.add_ref==TEST_DS_BASE+0x1994u);
        assert(out.enter_cs==TEST_CORE_BASE+0x8ea0u && out.leave_cs==TEST_CORE_BASE+0x8efcu);
        assert(out.buffer_vtable==ds_alias(owner,0x11ccu));
        assert(out.buffer_release==TEST_DS_BASE+0x2910u && out.buffer_get_caps==TEST_DS_BASE+0x29fcu);
        assert(out.buffer_lock==TEST_DS_BASE+0x2cbcu && out.buffer_unlock==TEST_DS_BASE+0x39acu);
        assert(out.buffer_play==TEST_DS_BASE+0x2e54u && out.buffer_stop==TEST_DS_BASE+0x3898u);
        assert(out.buffer_get_status==TEST_DS_BASE+0x2c2cu && out.buffer_get_position==TEST_DS_BASE+0x2a44u);
        assert(m.count==before.count && m.page_count==before.page_count &&
               !memcmp(m.cells,before.cells,sizeof(m.cells)) &&
               !memcmp(m.pages,before.pages,sizeof(m.pages)));
        ++cases;
    }
    /* Names are unneeded publications; poisoned pointers remain unread. */
    fixture(&m,2); put(&m,TEST_DS_NODE+8u,0xa0800000u);
    put(&m,TEST_SN_NODE+8u,0xac012000u);
    assert(bind(&m,&out)==KUI_ARMADA_CE_LIVE_OK); ++cases;
    unsigned successful_reads=m.reads;
    /* A callback failure at any observed step must not publish a partial
     * COM object, or turn an unreadable candidate into a valid one. */
    for(unsigned at=1;at<=successful_reads;at++) {
        fixture(&m,2); m.fail_at=at;
        expect(&m,KUI_ARMADA_CE_LIVE_READ); assert(m.reads==at); ++cases;
    }
    for(unsigned guard=0;guard<3u;guard++) {
        static const uint32_t offsets[3]={0x995d4u,0xce900u,0x996a0u};
        fixture(&m,2); uint32_t va=owner_slot(2)|offsets[guard];
        virtual_put(&m,va,virtual_get(&m,va)^1u);
        expect(&m,KUI_ARMADA_CE_LIVE_OWNER); ++cases;
    }
    const uint32_t bad_nodes[]={0u,0x8c000000u,0xac300100u,0x8d000000u,
                                0xa0800000u,0x8c300101u,0x8c307ffcu};
    for(unsigned i=0;i<sizeof(bad_nodes)/sizeof(bad_nodes[0]);i++) {
        fixture(&m,2); put(&m,KUI_ARMADA_CE_LIVE_MODULE_HEAD,bad_nodes[i]);
        expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
    }
    fixture(&m,2); put(&m,TEST_DS_NODE,TEST_DS_NODE+4u);
    expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
    fixture(&m,2); put(&m,TEST_SN_NODE+4u,TEST_DS_NODE);
    expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
    fixture(&m,2); put(&m,TEST_SN_NODE+4u,0x8c300300u);
    module(&m,0x8c300400u,0,TEST_DS_BASE,TEST_DS_SECTIONS,
           KUI_ARMADA_CE_LIVE_DSOUND_TOC,2,true);
    put(&m,TEST_SN_NODE+4u,0x8c300400u);
    expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
    for(unsigned which=0;which<3u;which++) {
        uint32_t node=which==0u?TEST_DS_NODE:which==1u?TEST_SN_NODE:TEST_CORE_NODE;
        fixture(&m,2); put(&m,node+100u,2u);
        expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
        fixture(&m,2); put(&m,node+96u,0x8c1416d4u);
        expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
    }
    for(unsigned field=0;field<6u;field++) {
        static const uint32_t offsets[6]={12u,80u,80u,112u,124u,176u};
        static const uint32_t values[6]={0u,0x01d70000u,0x03d71000u,2u,0xa000u,0x1d58000u};
        fixture(&m,2); put(&m,TEST_DS_NODE+offsets[field],values[field]);
        expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
    }
    for(unsigned row=0;row<3u;row++) for(unsigned field=0;field<4u;field++) {
        static const uint32_t offsets[4]={0u,4u,8u,16u};
        fixture(&m,2); uint32_t at=TEST_DS_SECTIONS+28u*row+offsets[field];
        virtual_put(&m,at,virtual_get(&m,at)^4u); expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
    }
    fixture(&m,2); put(&m,TEST_DS_NODE+176u,0xac310000u);
    expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
    fixture(&m,2); put(&m,owner_process(2),1u);
    expect(&m,KUI_ARMADA_CE_LIVE_OWNER); ++cases;
    fixture(&m,2); put(&m,owner_process(2)+12u,0x04000000u);
    expect(&m,KUI_ARMADA_CE_LIVE_OWNER); ++cases;
    fixture(&m,2); put(&m,owner_process(2)+12u,0);
    expect(&m,KUI_ARMADA_CE_LIVE_OWNER); ++cases;
    for(unsigned which=0;which<3u;which++) {
        fixture(&m,2);
        put(&m,(which==0u?TEST_DS_NODE:which==1u?TEST_SN_NODE:TEST_CORE_NODE)+20u,0);
        expect(&m,KUI_ARMADA_CE_LIVE_OWNER); ++cases;
    }
    const uint32_t bad_objects[]={0u,0xfffcu,0x00123401u,0xac123400u,
                                 0xa0800000u,0x8c000000u,0x8d000000u};
    for(unsigned i=0;i<sizeof(bad_objects)/sizeof(bad_objects[0]);i++) {
        fixture(&m,2); virtual_put(&m,owner_slot(2)|0xf7180u,bad_objects[i]);
        expect(&m,KUI_ARMADA_CE_LIVE_OBJECT); ++cases;
    }
    fixture(&m,2); virtual_put(&m,ds_alias(2,0x8110u),(owner_slot(2)|TEST_DEVICE)+4u);
    expect(&m,KUI_ARMADA_CE_LIVE_OBJECT); ++cases;
    const uint32_t bad_refs[]={0u,0x80000000u,0xffffffffu};
    for(unsigned i=0;i<sizeof(bad_refs)/sizeof(bad_refs[0]);i++) {
        fixture(&m,2); virtual_put(&m,(owner_slot(2)|TEST_DEVICE)+8u,bad_refs[i]);
        expect(&m,KUI_ARMADA_CE_LIVE_OBJECT); ++cases;
    }
    for(unsigned method=0;method<5u;method++) {
        fixture(&m,2);
        uint32_t va=method==0u?(owner_slot(2)|0xf21a8u):
            method==1u?(owner_slot(2)|TEST_DEVICE):
            ds_alias(2,0x116cu)+(method==2u?8u:method==3u?12u:24u);
        virtual_put(&m,va,virtual_get(&m,va)+4u);
        expect(&m,KUI_ARMADA_CE_LIVE_OBJECT); ++cases;
    }
    /* Both exact aliases are admitted, but a function in a different
     * process's copy of the DLL is not a substitute. */
    for(unsigned form=0;form<3u;form++) {
        fixture(&m,2); uint32_t low=TEST_DS_BASE&0x01ffffffu;
        uint32_t addresses[5]={owner_slot(2)|0xf21a8u,owner_slot(2)|TEST_DEVICE,
            ds_alias(2,0x116cu)+8u,ds_alias(2,0x116cu)+12u,ds_alias(2,0x116cu)+24u};
        uint32_t offsets[5]={0x15ecu,0x116cu,0x199cu,0x19dcu,0x1bccu};
        for(unsigned i=0;i<5u;i++) virtual_put(&m,addresses[i],
            form==0u?low+offsets[i]:form==1u?owner_slot(2)|(low+offsets[i]):
            TEST_DS_BASE+offsets[i]);
        if(form==2u) {
            /* This form also exercises an explicitly addressed vtable. */
            virtual_put(&m,TEST_DS_BASE+0x116cu+4u,TEST_DS_BASE+0x1994u);
            for(unsigned i=2;i<5u;i++) virtual_put(&m,TEST_DS_BASE+0x116cu+
                (i==2u?8u:i==3u?12u:24u),TEST_DS_BASE+offsets[i]);
            static const uint32_t secondary_offsets[8]={8u,12u,44u,76u,48u,72u,36u,16u};
            static const uint32_t secondary_targets[8]={0x2910u,0x29fcu,0x2cbcu,0x39acu,
                                                       0x2e54u,0x3898u,0x2c2cu,0x2a44u};
            for(unsigned i=0;i<8u;i++) virtual_put(&m,TEST_DS_BASE+0x11ccu+secondary_offsets[i],
                                                   TEST_DS_BASE+secondary_targets[i]);
        }
        assert(bind(&m,&out)==KUI_ARMADA_CE_LIVE_OK); ++cases;
    }
    fixture(&m,2); virtual_put(&m,owner_slot(2)|0xf21a8u,ds_alias(3,0x15ecu));
    expect(&m,KUI_ARMADA_CE_LIVE_OBJECT); ++cases;
    fixture(&m,2); put(&m,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,owner_process(1));
    assert(bind(&m,&out)==KUI_ARMADA_CE_LIVE_OK);
    assert(out.current_process!=out.owner_process && out.owner_index==2u); ++cases;
    /* Two independently matching process identities are ambiguous. */
    fixture(&m,2);
    put(&m,TEST_DS_NODE+12u,(1u<<2)|(1u<<3));
    put(&m,TEST_SN_NODE+12u,(1u<<2)|(1u<<3));
    put(&m,TEST_CORE_NODE+12u,(1u<<2)|(1u<<3));
    put(&m,owner_process(3)+12u,owner_slot(3));
    put(&m,TEST_DS_NODE+20u,0x00020002u); put(&m,TEST_SN_NODE+20u,0x00020002u);
    put(&m,TEST_CORE_NODE+20u,0x00020002u);
    game_identity(&m,3); expect(&m,KUI_ARMADA_CE_LIVE_OWNER); ++cases;
    /* Repeated metadata must retain the values used for the observation. */
    const uint32_t publications[]={KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,
        0x8c14d430u,0x8c14d434u,KUI_ARMADA_CE_LIVE_MODULE_HEAD,
        TEST_DS_NODE,TEST_DS_NODE+4u,TEST_DS_NODE+96u,TEST_DS_NODE+100u,
        TEST_SN_NODE,TEST_SN_NODE+4u,TEST_SN_NODE+96u,TEST_SN_NODE+100u,
        TEST_DS_NODE+12u,TEST_DS_NODE+80u,TEST_DS_NODE+124u,TEST_DS_NODE+176u,
        TEST_DS_NODE+112u,TEST_DS_NODE+84u,TEST_SN_NODE+12u,TEST_SN_NODE+80u,
        TEST_SN_NODE+124u,TEST_SN_NODE+176u,TEST_SN_NODE+112u,TEST_SN_NODE+84u,
        owner_process(2),owner_process(2)+12u};
    for(unsigned i=0;i<sizeof(publications)/sizeof(publications[0]);i++) {
        fixture(&m,2); m.change_address=publications[i]; m.change_on=2u; m.change_mask=4u;
        expect(&m,KUI_ARMADA_CE_LIVE_CHANGED); ++cases;
    }
    for(unsigned which=0;which<2u;which++) {
        fixture(&m,2); m.change_address=(which?TEST_SN_NODE:TEST_DS_NODE)+20u;
        m.change_on=2u; m.change_mask=2u;
        expect(&m,KUI_ARMADA_CE_LIVE_CHANGED); ++cases;
    }
    const uint32_t object_words[]={owner_slot(2)|0xf7180u,owner_slot(2)|0xf21a8u,
        owner_slot(2)|TEST_DEVICE,(owner_slot(2)|TEST_DEVICE)+8u,
        ds_alias(2,0x116cu)+8u,ds_alias(2,0x116cu)+12u,ds_alias(2,0x116cu)+24u,
        ds_alias(2,0x8110u)};
    for(unsigned i=0;i<sizeof(object_words)/sizeof(object_words[0]);i++) {
        fixture(&m,2); m.change_address=physical(&m,object_words[i]);
        m.change_on=2u; m.change_mask=4u;
        expect(&m,KUI_ARMADA_CE_LIVE_CHANGED); ++cases;
    }
    for(unsigned range=0;range<3u;range++) {
        static const uint32_t offsets[3]={0x995d4u,0xce900u,0x996a0u};
        fixture(&m,2); m.change_address=physical(&m,owner_slot(2)|offsets[range]);
        m.change_on=2u; m.change_mask=1u;
        expect(&m,KUI_ARMADA_CE_LIVE_CHANGED); ++cases;
    }
    /* The list budget admits exactly 64 stable links; a 65th link is never
     * dereferenced, even if it has a fully readable fixture record. */
    for(unsigned extras=61u;extras<=62u;extras++) {
        fixture(&m,2); put(&m,KUI_ARMADA_CE_LIVE_MODULE_HEAD,0x8c300400u);
        for(unsigned i=0;i<extras;i++) {
            uint32_t node=0x8c300400u+0x100u*i;
            put(&m,node,node); put(&m,node+4u,i+1u==extras?TEST_DS_NODE:node+0x100u);
            put(&m,node+96u,0); put(&m,node+100u,0);
        }
        if(extras==61u) assert(bind(&m,&out)==KUI_ARMADA_CE_LIVE_OK);
        else expect(&m,KUI_ARMADA_CE_LIVE_BUDGET);
        ++cases;
    }
    /* Fully mapped near-matching candidates exercise the physical read
     * budget, independent of the module-count and witness-page limits. */
    fixture(&m,2); put(&m,TEST_DS_NODE+12u,UINT32_MAX);
    put(&m,TEST_SN_NODE+12u,UINT32_MAX);
    put(&m,TEST_CORE_NODE+12u,UINT32_MAX);
    for(unsigned i=0;i<16u;i++) {
        put(&m,TEST_DS_NODE+16u+4u*i,0x00020002u);
        put(&m,TEST_SN_NODE+16u+4u*i,0x00020002u);
        put(&m,TEST_CORE_NODE+16u+4u*i,0x00020002u);
    }
    for(unsigned i=0;i<32u;i++) {
        put(&m,owner_process(i)+12u,owner_slot(i)); game_identity(&m,i);
        if(i!=2u) {
            uint32_t va=owner_slot(i)|0x996a0u;
            virtual_put(&m,va,virtual_get(&m,va)^1u);
        }
    }
    expect(&m,KUI_ARMADA_CE_LIVE_BUDGET);
    assert(m.reads==KUI_ARMADA_CE_LIVE_READ_LIMIT); ++cases;
    /* CCR incompatibility is rejected before even kernel publications are
     * sampled. Compatible modes with an uncached or wrongly colored CE
     * mapping must not sample the mapped holder. */
    const uint32_t bad_ccr[]={0u,0x104u,0x125u,0x185u,0x1a5u};
    static struct kui_armada_ce_live_workspace work;
    for(unsigned i=0;i<sizeof(bad_ccr)/sizeof(bad_ccr[0]);i++) {
        fixture(&m,2); memset(&out,0xa5,sizeof(out));
        assert(kui_armada_ce_live_sound_bind(TEST_TTB,bad_ccr[i],read_word,&m,
               &work,&out)==KUI_ARMADA_CE_LIVE_CACHE);
        assert(!m.reads && !out.reads && !out.device); ++cases;
    }
    for(unsigned which=0;which<2u;which++) {
        fixture(&m,2); uint32_t va=owner_slot(2)|0x995d4u,at=page_entry(&m,va);
        uint32_t holder=physical(&m,va),pte=get(&m,at);
        put(&m,at,which?pte^0x1000u:pte&~8u);
        expect(&m,KUI_ARMADA_CE_LIVE_CACHE);
        for(unsigned i=0;i<m.reads;i++) assert(m.addresses[i]!=holder);
        ++cases;
    }
    /* An explicit P0 section table follows the same checked translation
     * path; malformed root/block/page publications cannot reach their
     * claimed destination, including P2 aliases and G2 endpoints. */
    uint32_t table=TEST_DS_SECTIONS;
    for(unsigned variant=0;variant<7u;variant++) {
        fixture(&m,2);
        for(unsigned row=0;row<3u;row++) for(unsigned col=0;col<4u;col++) {
            static const uint32_t offsets[4]={0u,4u,8u,16u};
            uint32_t offset=28u*row+offsets[col];
            virtual_put(&m,table+offset,virtual_get(&m,TEST_DS_SECTIONS+offset));
        }
        put(&m,TEST_DS_NODE+176u,table);
        uint32_t root_at=TEST_TTB+4u*(table>>25),slot_table=get(&m,root_at);
        uint32_t block_at=slot_table+4u*((table>>16)&511u),block=get(&m,block_at);
        uint32_t pte_at=block+12u+4u*((table>>12)&15u);
        if(variant==0u) {
            assert(bind(&m,&out)==KUI_ARMADA_CE_LIVE_OK); ++cases; continue;
        }
        if(variant==1u) put(&m,root_at,slot_table|0x20000000u);
        if(variant==2u) put(&m,block_at,block|0x20000000u);
        if(variant==3u) put(&m,pte_at,0x00800079u);
        if(variant==4u) put(&m,pte_at,0x0c000079u);
        if(variant==5u) put(&m,pte_at,get(&m,pte_at)&~1u);
        if(variant==6u) {
            m.change_address=pte_at; m.change_on=2u; m.change_mask=0x4000u;
        }
        expect(&m,variant==6u?KUI_ARMADA_CE_LIVE_CHANGED:KUI_ARMADA_CE_LIVE_MAPPING);
        ++cases;
    }
    /* An object mapping may become different between independently stable
     * strict walks. Remembered page witnesses must reject that remap. */
    fixture(&m,2); uint32_t object_pte=page_entry(&m,owner_slot(2)|TEST_DEVICE);
    m.change_address=object_pte; m.change_on=3u; m.change_mask=0x4000u;
    m.change_persistent=true;
    uint32_t remapped=(get(&m,object_pte)^0x4000u)&0xfffff000u;
    put(&m,remapped|0x80000408u,3u);
    expect(&m,KUI_ARMADA_CE_LIVE_CHANGED); ++cases;
    const uint32_t bad_ttb[]={0u,0x8c000000u,0xac1458a0u,0x8d000000u,
                             0xa0800000u,0x8c1458a1u};
    for(unsigned i=0;i<sizeof(bad_ttb)/sizeof(bad_ttb[0]);i++) {
        fixture(&m,2); memset(&out,0xa5,sizeof(out));
        assert(kui_armada_ce_live_sound_bind(bad_ttb[i],TEST_CCR,read_word,&m,
               &work,&out)==KUI_ARMADA_CE_LIVE_ARGUMENT);
        assert(!m.reads && !out.reads && !out.device); ++cases;
    }
    fixture(&m,2);
    assert(kui_armada_ce_live_sound_bind(TEST_TTB,TEST_CCR,NULL,&m,&work,&out)==KUI_ARMADA_CE_LIVE_ARGUMENT);
    assert(!m.reads && !out.reads && !out.device); ++cases;
    assert(kui_armada_ce_live_sound_bind(TEST_TTB,TEST_CCR,read_word,&m,NULL,&out)==KUI_ARMADA_CE_LIVE_ARGUMENT);
    assert(!m.reads && !out.reads && !out.device); ++cases;
    assert(kui_armada_ce_live_sound_bind(TEST_TTB,TEST_CCR,read_word,&m,&work,NULL)==KUI_ARMADA_CE_LIVE_ARGUMENT);
    assert(!m.reads); ++cases;
    const uint32_t bad_current[]={0u,0x8c000000u,owner_process(2)+4u,
        KUI_ARMADA_CE_LIVE_PROCESS_BASE-4u,
        KUI_ARMADA_CE_LIVE_PROCESS_BASE+32u*KUI_ARMADA_CE_LIVE_PROCESS_STRIDE,
        owner_process(2)|0x20000000u,0xa0800000u};
    for(unsigned i=0;i<sizeof(bad_current)/sizeof(bad_current[0]);i++) {
        fixture(&m,2); put(&m,KUI_ARMADA_CE_LIVE_CURRENT_PROCESS,bad_current[i]);
        expect(&m,KUI_ARMADA_CE_LIVE_OWNER); ++cases;
    }
    /* COREDLL has a distinct four-section contract, and native CS imports
     * are accepted only with the reviewed function identity. */
    fixture(&m,2); put(&m,TEST_CORE_NODE+112u,3u);
    expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
    fixture(&m,2); put(&m,TEST_CORE_NODE+124u,0x9000u);
    expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
    fixture(&m,2); put(&m,TEST_CORE_NODE+12u,1u<<3);
    expect(&m,KUI_ARMADA_CE_LIVE_OWNER); ++cases;
    for(unsigned which=0;which<3u;which++) {
        uint32_t node=which==0u?TEST_DS_NODE:which==1u?TEST_SN_NODE:TEST_CORE_NODE;
        uint32_t table_at=which==0u?TEST_DS_SECTIONS:which==1u?TEST_SN_SECTIONS:TEST_CORE_SECTIONS;
        for(unsigned row=0;row<(which==2u?4u:3u);row++) for(unsigned field=0;field<4u;field++) {
            static const uint32_t offsets[4]={0u,4u,8u,16u};
            uint32_t at=table_at+28u*row+offsets[field];
            fixture(&m,2); virtual_put(&m,at,virtual_get(&m,at)^4u);
            expect(&m,KUI_ARMADA_CE_LIVE_MODULE); ++cases;
            fixture(&m,2); m.change_address=physical(&m,at); m.change_on=2u; m.change_mask=4u;
            expect(&m,KUI_ARMADA_CE_LIVE_CHANGED); ++cases;
        }
        fixture(&m,2); m.change_address=node+20u; m.change_on=2u; m.change_mask=1u;
        expect(&m,KUI_ARMADA_CE_LIVE_CHANGED); ++cases;
    }
    const uint32_t core_publications[]={TEST_CORE_NODE,TEST_CORE_NODE+4u,TEST_CORE_NODE+96u,
        TEST_CORE_NODE+100u,TEST_CORE_NODE+12u,TEST_CORE_NODE+80u,TEST_CORE_NODE+124u,
        TEST_CORE_NODE+176u,TEST_CORE_NODE+112u,TEST_CORE_NODE+84u};
    for(unsigned i=0;i<sizeof(core_publications)/sizeof(core_publications[0]);i++) {
        fixture(&m,2); m.change_address=core_publications[i]; m.change_on=2u; m.change_mask=4u;
        expect(&m,KUI_ARMADA_CE_LIVE_CHANGED); ++cases;
    }
    for(unsigned which=0;which<2u;which++) {
        uint32_t native=TEST_CORE_BASE+(which?0x8efcu:0x8ea0u);
        fixture(&m,2); virtual_put(&m,native,virtual_get(&m,native)^1u);
        expect(&m,KUI_ARMADA_CE_LIVE_IDENTITY); ++cases;
        fixture(&m,2); m.change_address=physical(&m,native); m.change_on=2u; m.change_mask=1u;
        expect(&m,KUI_ARMADA_CE_LIVE_CHANGED); ++cases;
    }
    for(unsigned which=0;which<11u;which++) {
        static const uint32_t secondary_offsets[8]={8u,12u,44u,76u,48u,72u,36u,16u};
        uint32_t va=which==0u?ds_alias(2,0x116cu)+4u:
            which<3u?ds_alias(2,0x801cu)+4u*(which-1u):
            ds_alias(2,0x11ccu)+secondary_offsets[which-3u];
        fixture(&m,2); virtual_put(&m,va,virtual_get(&m,va)^4u);
        expect(&m,KUI_ARMADA_CE_LIVE_OBJECT); ++cases;
        fixture(&m,2); m.change_address=physical(&m,va); m.change_on=2u; m.change_mask=4u;
        expect(&m,KUI_ARMADA_CE_LIVE_CHANGED); ++cases;
    }
    for(unsigned form=0;form<3u;form++) {
        fixture(&m,2); uint32_t low=TEST_CORE_BASE&0x01ffffffu;
        virtual_put(&m,ds_alias(2,0x801cu),form==0u?low+0x8ea0u:
            form==1u?core_alias(2,0x8ea0u):TEST_CORE_BASE+0x8ea0u);
        virtual_put(&m,ds_alias(2,0x8020u),form==0u?low+0x8efcu:
            form==1u?core_alias(2,0x8efcu):TEST_CORE_BASE+0x8efcu);
        assert(bind(&m,&out)==KUI_ARMADA_CE_LIVE_OK); ++cases;
    }
    /* Every newly exposed callable target accepts exactly the audited low,
     * owner-slot and module-base forms. Another process's explicit copy must
     * never be silently normalized into the owner. */
    for(unsigned form=0;form<4u;form++) {
        static const uint32_t buffer_offsets[8]={8u,12u,44u,76u,48u,72u,36u,16u};
        static const uint32_t buffer_targets[8]={0x2910u,0x29fcu,0x2cbcu,0x39acu,
                                               0x2e54u,0x3898u,0x2c2cu,0x2a44u};
        for(unsigned which=0;which<11u;which++) {
            fixture(&m,2);
            uint32_t va=which==0u?ds_alias(2,0x116cu)+4u:
                which<3u?ds_alias(2,0x801cu)+4u*(which-1u):
                ds_alias(2,0x11ccu)+buffer_offsets[which-3u];
            uint32_t rva=which==0u?0x1994u:which==1u?0x8ea0u:
                which==2u?0x8efcu:buffer_targets[which-3u];
            uint32_t base=(which==1u || which==2u)?TEST_CORE_BASE:TEST_DS_BASE;
            uint32_t low=base&0x01ffffffu;
            virtual_put(&m,va,form==0u?low+rva:form==1u?owner_slot(2)|(low+rva):
                form==2u?base+rva:owner_slot(3)|(low+rva));
            expect(&m,form==3u?KUI_ARMADA_CE_LIVE_OBJECT:KUI_ARMADA_CE_LIVE_OK);
            ++cases;
        }
    }
    /* A malformed caller cannot overlap the publication record with the
     * mutable workspace and accidentally return apparently usable targets. */
    fixture(&m,2); memset(&work,0xa5,sizeof(work));
    struct kui_armada_ce_live_sound *aliased=(void *)&work;
    assert(kui_armada_ce_live_sound_bind(TEST_TTB,TEST_CCR,read_word,&m,
           &work,aliased)==KUI_ARMADA_CE_LIVE_ARGUMENT);
    assert(!m.reads);
    const unsigned char *aliased_bytes=(const unsigned char *)(const void *)aliased;
    for(size_t i=0;i<sizeof(*aliased);i++) assert(!aliased_bytes[i]);
    ++cases;
    printf("Armada CE live sound: %u cases passed; ROM %u reads, disk max %u reads (cap %u); "
           "synthetic guards, read-only P1 callbacks.\n",cases,successful_reads,disk_reads,KUI_ARMADA_CE_LIVE_READ_LIMIT);
    return 0;
}
