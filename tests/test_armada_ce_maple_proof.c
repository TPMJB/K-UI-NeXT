/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kui/armada_ce_maple_proof.h"
enum {CAP=2300};
struct cell {uint32_t address,value,visits;};
struct cache_word {uint32_t va,p1,value;};
struct fixture {
    struct cell cell[CAP];struct cache_word cached[10];
    uint32_t cells,calls,fail_call,mutate_address,mutate_visit,mutate_value;
    uint32_t hit_code,dirty_alias;
};
static struct fixture f;
static struct kui_armada_ce_maple_snapshot old_snapshot;
static struct kui_armada_ce_maple_workspace old_workspace;
static struct kui_armada_ce_maple_proof_snapshot proof;
static struct kui_armada_ce_maple_proof_workspace workspace;
static const uint32_t head=0x8cc8e000u,module=0x8cfca408u,process=0x8c14e8b4u;
static const uint32_t base=0x03dd0000u,owner_slot=0x08000000u;
static const uint32_t ttb=0x8c1458a0u,table=0x8cf00000u,owner_table=0x8cf01000u;
static const uint32_t block=0x8cf02000u,owner_block=0x8cf03000u;
/* Synthetic bytes with the source fingerprint's CRC. These are deliberately
 * not private SH instructions. The independent bit-at-a-time oracle below
 * verifies all 32 bytes; an optional local owner kernel verifies the source. */
static const uint32_t synthetic[8]={0x7a55300bu,0x0ee9c49fu,0xa27d5833u,0x3611ecc7u,
    0xcaa5805bu,0x5e3914efu,0xf2cda883u,0x63397ec1u};
static struct cell *find(uint32_t a) {
    for(unsigned i=0;i<f.cells;i++) if(f.cell[i].address==a) return &f.cell[i];
    return NULL;
}
static void put(uint32_t a,uint32_t v) {
    struct cell *c=find(a);if(c) c->value=v;
    else {assert(f.cells<CAP);f.cell[f.cells++]=(struct cell){a,v,0};}
}
static uint32_t value(uint32_t a) {struct cell *c=find(a);assert(c);return c->value;}
static uint32_t visits(uint32_t a) {struct cell *c=find(a);return c?c->visits:0;}
static bool finish(uint32_t a,uint32_t v,uint32_t *out) {
    ++f.calls;
    if(f.fail_call && f.calls==f.fail_call) return false;
    struct cell *c=find(a);
    if(!c) {put(a,v);c=find(a);}
    ++c->visits;
    *out=a==f.mutate_address && c->visits==f.mutate_visit?f.mutate_value:v;
    return true;
}
static bool read_ram(void *context,uint32_t a,uint32_t *out) {
    assert(context==&f && out && !(a&3u));
    struct cell *c=find(a);
    assert(c); /* No arbitrary pointers, code backing or root values followed. */
    return finish(a,c->value,out);
}
static bool read_cache(void *context,uint32_t a,uint32_t *out) {
    assert(context==&f && out && !(a&3u));
    uint32_t v=0;
    if((a&0xffffc01fu)==0xf4000000u) {
        if(f.hit_code || f.dirty_alias) for(unsigned i=0;i<8u;i++) {
            const struct cache_word *c=&f.cached[i];unsigned color=(c->va>>12)&3u;
            if((a&0xfe0u)!=(c->p1&0xfe0u)) continue;
            unsigned requested=(a>>12)&3u;
            if(f.hit_code && requested==color) v=(c->p1&0x1ffffc00u)|3u;
            if(f.dirty_alias && requested==((color+1u)&3u)) v=(c->p1&0x1ffffc00u)|3u;
        }
    } else if((a&0xffffc003u)==0xf5000000u) {
        bool found=false;
        for(unsigned i=0;i<8u;i++) {
            const struct cache_word *c=&f.cached[i];
            uint32_t ca=0xf5000000u|(c->p1&0xfe0u)|(((c->va>>12)&3u)<<12)|(c->p1&0x1cu);
            if(a==ca) {v=c->value;found=true;break;}
        }
        assert(found && f.hit_code);
    } else {
        assert((a&0xff000000u)==0xac000000u);
        struct cell *c=find(a^0x20000000u);assert(c);v=c->value;
    }
    return finish(a,v,out);
}
static void node(uint32_t a,uint32_t next,bool is_maple) {
    put(a,a);put(a+4u,next);put(a+12u,is_maple?8u:0u);put(a+80u,is_maple?base:0u);
    put(a+84u,0x210eu);put(a+96u,is_maple?KUI_ARMADA_CE_MAPLE_ROM_TOC:0x12345678u);
    put(a+100u,is_maple?3u:2u);put(a+112u,is_maple?3u:0u);
    put(a+124u,is_maple?0xa000u:0u);put(a+176u,is_maple?base+0x72ccu:0u);
}
static void reset(void) {
    memset(&f,0,sizeof(f));memset(&old_snapshot,0,sizeof(old_snapshot));memset(&old_workspace,0,sizeof(old_workspace));
    memset(&proof,0,sizeof(proof));memset(&workspace,0,sizeof(workspace));
    put(KUI_ARMADA_CE_MAPLE_TTB,ttb);put(KUI_ARMADA_CE_MAPLE_PTEH,0xfffffc03u);put(KUI_ARMADA_CE_MAPLE_CCR,0x105u);
    put(KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS,process);put(KUI_ARMADA_CE_MAPLE_HEAP_LOW,head);
    put(KUI_ARMADA_CE_MAPLE_HEAP_HIGH,0x8cfd0000u);put(KUI_ARMADA_CE_MAPLE_MODULE_HEAD,head);
    for(unsigned i=0;i<15u;i++) node(head+i*0x100u,i==14u?module:head+(i+1u)*0x100u,false);
    node(module,0u,true);put(module+20u,0x00010000u);
    put(process,0x12340003u);put(process+8u,0x42u);put(process+12u,owner_slot);
    put(KUI_ARMADA_CE_MAPLE_MDSTAR,0x0cfd3000u);put(KUI_ARMADA_CE_MAPLE_MDTSEL,1u);
    put(KUI_ARMADA_CE_MAPLE_MDEN,1u);put(KUI_ARMADA_CE_MAPLE_MDST,1u);put(KUI_ARMADA_CE_MAPLE_ISTNRM,0x80000000u);
    assert(kui_armada_ce_maple_observe(&old_workspace,&old_snapshot,read_ram,&f)==KUI_ARMADA_CE_MAPLE_MISSING);
    assert(old_workspace.nodes==16u && old_snapshot.counts[3]==0x153u && old_snapshot.maple[0]==module && !old_snapshot.mapledev[0]);
    put(ttb+4u,table);put(ttb+16u,owner_table);
    put(table+0x774u,block);put(owner_table+0x774u,owner_block);
    put(block+0x20u,0x0c320079u);put(block+0x28u,0x0c330079u);put(owner_block+0x28u,0x0c340079u);
    for(unsigned i=0;i<8u;i++) {
        f.cached[i]=(struct cache_word){base+0x5b0cu+4u*i,0x8c320b0cu+4u*i,synthetic[i]};
        put(f.cached[i].p1,synthetic[i]);
    }
    f.cached[8]=(struct cache_word){0x09dd72b8u,0x8c3402b8u,0x8cfd4000u};
    f.cached[9]=(struct cache_word){0x03dd72b8u,0x8c3302b8u,0x7fff0000u};
    put(f.cached[8].p1,f.cached[8].value);put(f.cached[9].p1,f.cached[9].value);
    for(unsigned i=0;i<f.cells;i++) f.cell[i].visits=0;
    f.calls=0;
}
static enum kui_armada_ce_maple_proof_status run(void) {
    struct kui_armada_ce_maple_snapshot before=old_snapshot;
    enum kui_armada_ce_maple_proof_status status=kui_armada_ce_maple_proof(&old_snapshot,&old_workspace,
        read_ram,read_cache,&f,&workspace,&proof);
    assert(!memcmp(&before,&old_snapshot,sizeof(before)));
    assert(proof.counts[0]==(uint32_t)status && proof.counts[1]==f.calls && f.calls<=2048u);
    return status;
}
static uint32_t oracle_crc(const unsigned char *bytes,size_t size) {
    uint32_t crc=UINT32_MAX;
    for(size_t i=0;i<size;i++) for(unsigned bit=0;bit<8u;bit++) {
        uint32_t low=(crc^(bytes[i]>>bit))&1u;
        crc>>=1;if(low) crc^=UINT32_C(0xedb88320);
    }
    return ~crc;
}
static void success_and_cache(void) {
    unsigned char bytes[32];
    for(unsigned i=0;i<32u;i++) bytes[i]=(unsigned char)(synthetic[i/4u]>>(8u*(i&3u)));
    assert(oracle_crc(bytes,sizeof(bytes))==KUI_ARMADA_CE_MAPLE_PROOF_CODE_CRC);
    reset();assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_OK);
    assert(proof.counts[2]==0x7bd7ff55u && proof.counts[3]==0x7bd7ff55u && proof.counts[4]==31u);
    assert(proof.code[0]==0x03dd5b0cu && proof.code[1]==table && proof.code[2]==block && proof.code[3]==0x0c320079u && proof.code[4]==0x8c320b0cu);
    assert(proof.owner[0]==process && proof.owner[1]==owner_slot && proof.owner[2]==0x42u && proof.owner[3]==8u && proof.owner[4]==0x09dd72b8u);
    assert(proof.root[0]==0x8c3402b8u && proof.root[1]==0x0c340079u && proof.root[2]==0x8cfd4000u);
    assert(proof.root[3]==0x8c3302b8u && proof.root[4]==0x7fff0000u);
    assert(!proof.context[0] && !proof.context[1] && !proof.context[2] && proof.context[3]==synthetic[0] && proof.context[4]==0x03dd72b8u);
    for(unsigned i=0;i<5u;i++) assert(!proof.fault[i]);
    assert(!visits(0x8cfd4000u) && !visits(0x7fff0000u));
    /* No special physical page is required for source code identity. */
    reset();f.hit_code=1u;
    for(unsigned i=0;i<8u;i++) put(f.cached[i].p1,0xdeadbeefu);
    assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_OK && !visits(f.cached[0].p1|0x20000000u));
    reset();f.hit_code=1u;f.dirty_alias=1u;assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CACHE);
    reset();put(f.cached[0].p1,synthetic[0]^1u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CODE);
    assert(proof.counts[3]!=proof.counts[2] && proof.context[1]==UINT32_MAX);
    /* Root values are raw data, including zero or an unsafe pointer. No
     * successful proof turns them into an admitted dereference. */
    reset();put(f.cached[8].p1,0xa05f6c88u);put(f.cached[9].p1,0u);
    assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_OK && proof.root[2]==0xa05f6c88u && !proof.root[4]);
    assert(!visits(0xa05f6c88u));
}
static void metadata_process_and_mapping(void) {
    reset();old_snapshot.counts[4]=KUI_ARMADA_CE_MAPLE_MODULE;
    assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA && !f.calls);
    reset();old_snapshot.maple[0]=0u;assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA && !f.calls);
    reset();old_workspace.node[15].field[5]^=4u;assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
    reset();old_workspace.node[15].field[6]=2u;assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_METADATA);
    reset();put(process,2u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_PROCESS);
    reset();put(process+12u,0x02000000u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_PROCESS);
    reset();put(process+8u,0u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_PROCESS);
    reset();put(process+8u,UINT32_MAX);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_PROCESS);
    reset();put(module+20u,1u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_PROCESS);
    reset();put(module+12u,1u);old_workspace.node[15].field[2]=1u;old_snapshot.maple[4]=1u;
    assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_PROCESS);
    reset();put(ttb+4u,0xac000000u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_MAPPING);
    assert(proof.code[0]==base+0x5b0cu && proof.code[1]==0xac000000u && !proof.code[4] && proof.context[0]==KUI_ARMADA_CE_RAM_BIND_SLOT);
    reset();put(table+0x774u,0x12345678u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_MAPPING);
    assert(proof.code[2]==0x12345678u && !proof.code[3] && !proof.code[4]);
    reset();put(block+0x20u,0x0c320078u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_MAPPING);
    assert(proof.code[3]==0x0c320078u && !proof.code[4]);
    reset();put(owner_block+0x28u,0u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_MAPPING);
    assert(proof.context[1]==KUI_ARMADA_CE_RAM_BIND_PAGE && proof.context[2]==UINT32_MAX);
    reset();put(block+0x28u,0u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_MAPPING);
    assert(proof.context[1]==KUI_ARMADA_CE_RAM_BIND_OK && proof.context[2]==KUI_ARMADA_CE_RAM_BIND_PAGE);
    /* Upper endpoint of the independently admitted PROCESS table. */
    reset();uint32_t last=KUI_ARMADA_CE_MAPLE_PROCESS_BASE+31u*KUI_ARMADA_CE_MAPLE_PROCESS_STRIDE;
    put(KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS,last);old_snapshot.cpu[3]=last;
    put(last,31u);put(last+8u,0x99u);put(last+12u,0x40000000u);
    put(module+12u,0x80000000u);old_workspace.node[15].field[2]=old_snapshot.maple[4]=0x80000000u;
    put(module+76u,0x00010000u);put(ttb+128u,owner_table);
    assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_OK && proof.owner[4]==0x41dd72b8u);
}
static void mutate(uint32_t address,uint32_t visit,uint32_t replacement) {
    f.mutate_address=address;f.mutate_visit=visit;f.mutate_value=replacement;
}
static void failures_and_witnesses(void) {
    reset();assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_OK);uint32_t total=f.calls;
    for(unsigned hit=0;hit<2u;hit++) for(uint32_t i=1;i<=total;i++) {
        reset();f.hit_code=hit;f.fail_call=i;assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_READ);
        assert(proof.counts[1]==i && proof.fault[0]==KUI_ARMADA_CE_MAPLE_PROOF_READ);
    }
    static const uint32_t globals[]={KUI_ARMADA_CE_MAPLE_TTB,KUI_ARMADA_CE_MAPLE_PTEH,KUI_ARMADA_CE_MAPLE_CCR,
        KUI_ARMADA_CE_MAPLE_CURRENT_PROCESS,KUI_ARMADA_CE_MAPLE_HEAP_LOW,KUI_ARMADA_CE_MAPLE_HEAP_HIGH,KUI_ARMADA_CE_MAPLE_MODULE_HEAD};
    for(unsigned i=0;i<7u;i++) {
        reset();mutate(globals[i],2u,value(globals[i])^4u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    }
    static const uint32_t offsets[]={0,4,12,80,84,96,100,112,124,176};
    for(unsigned i=0;i<10u;i++) {
        reset();mutate(module+offsets[i],2u,value(module+offsets[i])^4u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    }
    reset();mutate(head+96u,2u,value(head+96u)^4u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    reset();mutate(process+8u,2u,0x43u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    reset();mutate(module+20u,2u,0x00020000u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    reset();mutate(ttb+4u,2u,owner_table);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    reset();mutate(block+0x20u,2u,0x0c350079u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    reset();mutate(block+0x20u,17u,0x0c350079u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    reset();mutate(f.cached[8].p1|0x20000000u,3u,f.cached[8].value^4u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    reset();mutate(0xf4000b00u,2u,0x0c320001u);assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
    printf("CE Maple proof callback failure positions checked: %u.\n",total);
}
static void largest_stable_collector(void) {
    reset();
    for(unsigned i=0;i<49u;i++) node(head+i*0x100u,i==48u?module:head+(i+1u)*0x100u,false);
    memset(&old_snapshot,0,sizeof(old_snapshot));memset(&old_workspace,0,sizeof(old_workspace));
    for(unsigned i=0;i<f.cells;i++) f.cell[i].visits=0;
    f.calls=0;
    assert(kui_armada_ce_maple_observe(&old_workspace,&old_snapshot,read_ram,&f)==KUI_ARMADA_CE_MAPLE_MISSING);
    assert(old_workspace.nodes==50u && old_snapshot.counts[3]==1019u);
    for(unsigned i=0;i<f.cells;i++) f.cell[i].visits=0;
    f.calls=0;
    assert(run()==KUI_ARMADA_CE_MAPLE_PROOF_OK && proof.counts[1]==1342u);
}
static void arguments_and_budget(void) {
    reset();assert(kui_armada_ce_maple_proof(NULL,&old_workspace,read_ram,read_cache,&f,&workspace,&proof)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(kui_armada_ce_maple_proof(&old_snapshot,&old_workspace,NULL,read_cache,&f,&workspace,&proof)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(kui_armada_ce_maple_proof(&old_snapshot,&old_workspace,read_ram,NULL,&f,&workspace,&proof)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(kui_armada_ce_maple_proof(&old_snapshot,&old_workspace,read_ram,read_cache,&f,&workspace,(struct kui_armada_ce_maple_proof_snapshot *)(void *)&old_snapshot)==KUI_ARMADA_CE_MAPLE_PROOF_ARGUMENT);
    assert(!f.calls);
    workspace.out=&proof;workspace.read32=read_ram;workspace.read_cache=read_cache;workspace.context=&f;workspace.reads=2048u;
    uint32_t word;
    assert(!kui_armada_ce_maple_proof_read(&workspace,KUI_ARMADA_CE_MAPLE_TTB,&word));
    assert(workspace.failed==KUI_ARMADA_CE_MAPLE_PROOF_BUDGET && !f.calls && workspace.reads==2048u);
    workspace.reads=0;
    assert(!kui_armada_ce_maple_proof_cache_read(&workspace,0xa05f6c88u,&word) && !f.calls);
    assert(!kui_armada_ce_maple_proof_read(&workspace,0xa05f6c88u,&word) && !f.calls);
}
static void owner_kernel(const char *path) {
    FILE *in=fopen(path,"rb");assert(in);unsigned char bytes[32];
    assert(fseek(in,0xa430cL,SEEK_SET)==0 && fread(bytes,1,sizeof(bytes),in)==sizeof(bytes));
    assert(fclose(in)==0 && oracle_crc(bytes,sizeof(bytes))==KUI_ARMADA_CE_MAPLE_PROOF_CODE_CRC);
    puts("Optional owner kernel's full 32-byte source fingerprint passed.");
}
int main(int argc,char **argv) {
    success_and_cache();metadata_process_and_mapping();failures_and_witnesses();largest_stable_collector();arguments_and_budget();
    if(argc>1) owner_kernel(argv[1]);
    puts("Passive CE Maple proof: photographed missing-MAPLEDEV fixture, coherent source identity, independent roots, raw refusals and repeated witnesses passed.");
    return 0;
}
