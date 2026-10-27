/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "kui/armada_ce_maple_traffic.h"
static const uint32_t TTB=0x8c1458a0u,PROC=0x8c14e8b4u,EXT=0x08030a60u,TX=0x0cfd3000u,RX=0x0cfd1000u;
struct fixture {
    struct {uint32_t at,value,visits;} cell[128];
    unsigned cells,calls,p1,cache,p2,fail,mutate,mutate_visit;
    uint32_t replacement;
} f;
static struct kui_armada_ce_maple_traffic_scratch scratch;
static struct kui_armada_ce_maple_traffic_snapshot out;
static unsigned checks;
#define CHECK(c) do {++checks;assert(c);} while(0)
static unsigned put(uint32_t at,uint32_t value) {
    if(at>=EXT && at<EXT+0x198u) at=0xacf93a60u+(at-EXT);
    for(unsigned i=0;i<f.cells;i++) if(f.cell[i].at==at) {f.cell[i].value=value;return i;}
    assert(f.cells<128u);unsigned i=f.cells++;f.cell[i].at=at;f.cell[i].value=value;return i;
}
static bool read_cell(void *context,uint32_t at,uint32_t *value,unsigned kind) {
    assert(context==&f && !(at&3u));++f.calls;
    if(kind==0) {++f.p1;CHECK((at>=0x8c010000u && at<0x8d000000u) || at==0xff000008u || at==0xff00001cu);}
    if(kind==1) {++f.cache;
        CHECK((at&0xffffc01fu)==0xf4000000u || (at&0xffffc003u)==0xf5000000u ||
            (at>=0xacf93a60u && at<0xacf93a60u+0x198u));}
    if(kind==2) {++f.p2;CHECK((at>=0xacfd3000u && at<0xacfd4000u) || (at>=0xacfd1000u && at<0xacfd3000u));}
    if(f.calls==f.fail) return false;
    for(unsigned i=0;i<f.cells;i++) if(f.cell[i].at==at) {
        ++f.cell[i].visits;
        if(f.mutate==i+1u && f.cell[i].visits==f.mutate_visit) f.cell[i].value=f.replacement;
        *value=f.cell[i].value;return true;
    }
    fprintf(stderr,"Unexpected read %08x kind%u\n",at,kind);assert(0);return false;
}
static bool p1(void *c,uint32_t at,uint32_t *v) {return read_cell(c,at,v,0);}
static bool cache(void *c,uint32_t at,uint32_t *v) {return read_cell(c,at,v,1);}
static bool p2(void *c,uint32_t at,uint32_t *v) {return read_cell(c,at,v,2);}
static void mapping(uint32_t va,uint32_t block,uint32_t physical,uint32_t flags) {
    put(0x8c100000u+4u*((va>>16)&511u),block);
    put(block+12u+4u*((va>>12)&15u),physical|flags);
}
static void descriptor(unsigned index,uint32_t off,unsigned words,bool last,unsigned unit,unsigned command) {
    unsigned port=index&3u;
    put((TX+off)|0xa0000000u,(last?0x80000000u:0u)|(port<<16)|words);
    put((TX+off+4u)|0xa0000000u,RX+32u*index);
    put((TX+off+8u)|0xa0000000u,(words<<24)|(port<<22)|(((port<<6)|unit)<<8)|command);
    put((RX+32u*index)|0xa0000000u,0x03000008u|((port<<6)<<8));
}
static void prepare(unsigned records) {
    memset(&f,0,sizeof(f));memset(&scratch,0,sizeof(scratch));memset(&out,0,sizeof(out));
    put(0xff000008u,TTB);put(0xff00001cu,0x105u);put(0x8c145890u,PROC);
    put(TTB,0x8c100000u);put(TTB+16u,0x8c100000u);
    mapping(EXT,0x8c110000u,0x0cf93000u,0x17du);
    mapping(0x08130000u,0x8c120000u,TX,0x175u);
    mapping(0x08100000u,0x8c130000u,RX,0x175u);
    mapping(0x08101000u,0x8c130000u,RX+0x1000u,0x175u);
    static const uint32_t fields[10]={0,0x84,0x88,0x8c,0x90,0x100,0xc0,0xc4,0xcc,0xd8};
    for(unsigned i=0;i<10u;i++) for(unsigned c=0;c<4u;c++)
        put(0xf4000000u|((0x8cf93a60u+fields[i])&0xfe0u)|(c<<12),0);
    put(EXT,0);put(EXT+0x84u,TX);put(EXT+0x88u,0x00130000u);
    put(EXT+0x100u,0x00120c00u); /* This MMIO pointer value is never followed. */
    put(EXT+0xc0u,RX);put(EXT+0xc4u,0);put(EXT+0xccu,0x00100000u);put(EXT+0xd8u,0x0ced1000u);
    uint32_t offset=0;
    for(unsigned i=0;i<records;i++) {
        unsigned words=i==0u?1u:0u;
        descriptor(i,offset,words,i+1u==records,32u,9u);
        put(EXT+0x8cu,0x00130000u+offset);offset+=12u+4u*words;
    }
    put(EXT+0x90u,0x00130000u+offset);
}
static unsigned run(void) {
    f.calls=f.p1=f.cache=f.p2=0;
    unsigned status=kui_armada_ce_maple_traffic_visit(0x30a60u,TTB,PROC,0x105u,p1,cache,p2,&f,&scratch,&out);
    CHECK(out.counts[0]==KUI_ARMADA_CE_MAPLE_TRAFFIC_MAGIC && out.counts[4]==f.calls && f.calls<=512u);
    CHECK((status==0u)==(out.fault[0]==0u));return status;
}
static void valid(void) {
    for(unsigned n=1;n<=16u;n++) {
        prepare(n);CHECK(run()==0);CHECK(out.counts[1]==1u && out.counts[2]==1u && out.counts[3]==0u);
        CHECK(f.p2==n*8u && f.cache==104u && !out.first[3] && !out.last[3]);
        CHECK(f.p1==59u && f.calls==163u+n*8u); /* Four-record visit291->195. */
        CHECK(out.metadata[1]==TX && out.metadata[2]==RX && out.metadata[4]==12u*n+4u);
    }
    prepare(1);descriptor(0,0,255u,true,1u,11u);put(EXT+0x90u,0x00130000u+1032u);
    CHECK(run()==0 && f.p2==8u && out.commands[2]==1u && out.first[3]==0xff00010bu);
    prepare(1);
    for(unsigned i=0;i<40u;i++) CHECK(run()==0);
    descriptor(0,0,1u,true,1u,11u);CHECK(run()==0 && out.commands[2]==1u);
    uint32_t first[5];memcpy(first,out.first,sizeof(first));
    descriptor(0,0,1u,true,2u,12u);CHECK(run()==0 && out.commands[3]==1u);
    CHECK(!memcmp(first,out.first,sizeof(first)) && out.last[3]==0x0100020cu);
    descriptor(0,0,1u,true,32u,9u);CHECK(run()==0 && out.last[3]==0x0100020cu);
    out.counts[1]=out.counts[2]=out.commands[1]=UINT32_MAX;
    descriptor(0,0,1u,true,2u,10u);CHECK(run()==0);
    CHECK(out.counts[1]==UINT32_MAX && out.counts[2]==UINT32_MAX && out.commands[1]==UINT32_MAX);
    static const unsigned command[5]={1,10,11,12,255};
    for(unsigned i=0;i<5u;i++) {prepare(1);descriptor(0,0,1,true,1,command[i]);CHECK(run()==0 && out.commands[i]==1u);}
}
static void coherent_cache(void) {
    const uint32_t physical=0x8cf93a60u,index=physical&0xfe0u,wanted=physical&0x1ffffc00u;
    prepare(1);
    put(EXT,1u); /* Backing RAM is stale, while the dirty matching VA color is current. */
    put(0xf4000000u|index,wanted|3u);put(0xf5000000u|index|(physical&0x1cu),0u);
    CHECK(run()==0);
    prepare(1);put(0xf4000000u|index|0x1000u,wanted|3u);
    CHECK(run()==KUI_ARMADA_CE_MAPLE_TRAFFIC_CACHE && !f.p2);
    prepare(1);unsigned tag=put(0xf4000000u|index,0u);
    f.mutate=tag+1u;f.mutate_visit=2u;f.replacement=wanted|1u;
    CHECK(run()==KUI_ARMADA_CE_MAPLE_TRAFFIC_CHANGED && !f.p2);
    /* Independent metadata passes select fresh sources. A dirty cache
     * line may be legitimately evicted/writeback before the final pass. */
    prepare(1);tag=put(0xf4000000u|index,wanted|3u);
    put(0xf5000000u|index,0u);f.mutate=tag+1u;f.mutate_visit=3u;f.replacement=0;
    CHECK(run()==0);
    prepare(1);put(EXT,1u);tag=put(0xf4000000u|index,wanted|3u);
    put(0xf5000000u|index,0u);f.mutate=tag+1u;f.mutate_visit=3u;f.replacement=0;
    CHECK(run()==KUI_ARMADA_CE_MAPLE_TRAFFIC_CHANGED && out.counts[2]==0u);
}
static void metadata_groups_and_budget(void) {
    static const uint32_t fields[10]={0,0x84,0x88,0x8c,0x90,0x100,0xc0,0xc4,0xcc,0xd8};
    static const unsigned groups[8]={4,4,5,5,6,6,6,5};
    for(unsigned shift=0;shift<32u;shift+=4u) {
        prepare(1);CHECK(run()==0);
        uint32_t va=EXT+shift,physical=0x8cf93a60u+shift;
        scratch.map[0].virtual_address=va;scratch.map[0].physical_address=physical;
        for(unsigned i=0;i<10u;i++) {
            put(va+fields[i],0xface0000u+i);
            for(unsigned c=0;c<4u;c++)
                put(0xf4000000u|((physical+fields[i])&0xfe0u)|(c<<12),0u);
        }
        f.calls=f.p1=f.cache=f.p2=0;scratch.reads=0;
        struct kui_armada_ce_maple_traffic_io io={p1,cache,p2,&f,&scratch};
        CHECK(kui_armada_ce_maple_traffic_meta(&io,va,0x105u,false));
        CHECK(f.calls==8u*groups[shift/4u]+20u && f.cache==f.calls && !f.p1 && !f.p2);
        for(unsigned i=0;i<10u;i++) CHECK(scratch.word[i]==0xface0000u+i);
        CHECK(kui_armada_ce_maple_traffic_meta(&io,va,0x105u,true));
        CHECK(f.calls==16u*groups[shift/4u]+40u);
    }
    /* A callback budget exhausted in the middle of the first line retains
     * the latched budget fault and does not publish a partial metadata word. */
    prepare(1);CHECK(run()==0);scratch.reads=KUI_ARMADA_CE_MAPLE_TRAFFIC_READ_LIMIT-5u;
    memset(scratch.error,0,sizeof(scratch.error));scratch.word[0]=UINT32_MAX;
    f.calls=f.p1=f.cache=f.p2=0;
    struct kui_armada_ce_maple_traffic_io io={p1,cache,p2,&f,&scratch};
    CHECK(!kui_armada_ce_maple_traffic_meta(&io,EXT,0x105u,false));
    CHECK(scratch.error[0]==KUI_ARMADA_CE_MAPLE_TRAFFIC_BUDGET && f.calls==5u &&
        scratch.reads==KUI_ARMADA_CE_MAPLE_TRAFFIC_READ_LIMIT && scratch.word[0]==UINT32_MAX);
}
static void read_failures(void) {
    prepare(16);CHECK(run()==0);unsigned calls=f.calls;
    for(unsigned at=1;at<=calls;at++) {
        prepare(16);f.fail=at;CHECK(run()!=0 && f.calls==at);
        CHECK(out.counts[2]==0u && out.counts[3]==1u && out.commands[0]==0u && !out.first[3]);
    }
    /* Every observed address is mutated on its final visit: capture cannot
     * commit a histogram or sample after the final witnesses disagree. */
    prepare(16);CHECK(run()==0);unsigned cells=f.cells;
    uint32_t visited[128];for(unsigned i=0;i<cells;i++) visited[i]=f.cell[i].visits;
    for(unsigned i=0;i<cells;i++) if(visited[i]>1u) {
        prepare(16);descriptor(0,0,1u,false,1u,11u);
        f.mutate=i+1u;f.mutate_visit=visited[i];f.replacement=f.cell[i].value^1u;
        CHECK(run()!=0 && out.counts[2]==0 && !out.first[3] && out.commands[2]==0);
    }
}
static void rejected(void) {
    static const struct {uint32_t at,value;} bad[]={
        {0x08030a60u,1u},{0x08030ae4u,0x0cfcf000u}, /* shutdown, reserved TX */
        {0x08030b20u,0x0cfce000u}, /* RX crosses reserved page */
        {0x08030b24u,1u},{0x08030b38u,0u}, /* RX high/bias */
        {0x08030ae8u,0x00130004u}, /* unaligned TX allocation */
        {0x08030aecu,0x0012fffcu},{0x08030af0u,0x00131004u}, /* list bounds */
        {0x8c10004cu,0u},{0x8c12000cu,0x0cfd317du}, /* missing table, cached TX */
        {0x8c130010u,0x0cfd4175u}, /* RX physical disagreement */
        {0x8c11000cu,0x0cfcf17du}, /* reserved extension */
        {0x8c1458b0u,0x8c200000u}, /* owner/slot0 mismatch */
        {0xacfd3000u,0x80000201u},{0xacfd3000u,1u}, /* special/missing last */
        {0xacfd3004u,0x0cfd0ffcu},{0xacfd3004u,0x0cfd3000u},{0xacfd3004u,0x8cfd1000u},
        {0xacfd3008u,0x0100030bu},{0xacfd3008u,0x0100600bu},{0xacfd3008u,0x0000200bu}
    };
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {prepare(1);put(bad[i].at,bad[i].value);CHECK(run()!=0);CHECK(!out.first[3] && !out.commands[2]);}
    prepare(16);put(0xacfd3000u,1u);put(EXT+0x8cu,0x00130000u+196u);put(EXT+0x90u,0x00130000u+208u);
    put(0xacfd30b8u,0x00030000u);descriptor(16,196u,0u,true,1u,11u);CHECK(run()==KUI_ARMADA_CE_MAPLE_TRAFFIC_BUDGET && f.p2==64u);
    prepare(1);out.counts[3]=UINT32_MAX;put(EXT,1u);CHECK(run()!=0 && out.counts[3]==UINT32_MAX);
    prepare(1);unsigned before=f.calls;
    CHECK(kui_armada_ce_maple_traffic_visit(0x30f00u,TTB,PROC,0x105u,p1,cache,p2,&f,&scratch,&out)!=0);
    CHECK(f.calls==before);
    prepare(1);CHECK(kui_armada_ce_maple_traffic_visit(0x30a60u,TTB,PROC+4u,0x105u,p1,cache,p2,&f,&scratch,&out)!=0 && !f.calls);
    prepare(1);CHECK(kui_armada_ce_maple_traffic_visit(0x30a60u,TTB,PROC,0x125u,p1,cache,p2,&f,&scratch,&out)!=0 && !f.calls);
    prepare(1);CHECK(kui_armada_ce_maple_traffic_visit(0x10030a60u,TTB,PROC,0x105u,p1,cache,p2,&f,&scratch,&out)!=0 && !f.calls);
    prepare(1);CHECK(kui_armada_ce_maple_traffic_visit(0x30a60u,0xac1458a0u,PROC,0x105u,p1,cache,p2,&f,&scratch,&out)!=0 && !f.calls);
    prepare(1);scratch.reads=512u;struct kui_armada_ce_maple_traffic_io io={p1,cache,p2,&f,&scratch};uint32_t v;
    CHECK(!kui_armada_ce_maple_traffic_table(&io,TTB,&v) && !f.calls && scratch.error[0]==KUI_ARMADA_CE_MAPLE_TRAFFIC_BUDGET);
    CHECK(kui_armada_ce_maple_traffic_visit(0x30a60u,TTB,PROC,0x105u,p1,cache,p2,&f,0,&out)==KUI_ARMADA_CE_MAPLE_TRAFFIC_ARGUMENT);
    CHECK(kui_armada_ce_maple_traffic_visit(0x30a60u,TTB,PROC,0x105u,p1,cache,p2,&f,&scratch,(void *)&scratch)==KUI_ARMADA_CE_MAPLE_TRAFFIC_ARGUMENT);
}
int main(void) {valid();coherent_cache();metadata_groups_and_budget();read_failures();rejected();printf("traffic sampler: %u checks passed; metadata200->104, four-record visit291->195, scratch %zu bytes\n",checks,sizeof(scratch));return 0;}
