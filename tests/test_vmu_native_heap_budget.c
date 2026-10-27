/* SPDX-License-Identifier: GPL-3.0-only */
/* External original instructions test the production cold adapter. No retail
 * executable is embedded. Every changed fixed instruction must refuse, while
 * semantically valid reserve values/lease sizes retain exact arithmetic. */
#include "kui/vmu_native_heap_budget.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,mutants,originals,found;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint16_t u16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static uint32_t u32(const uint8_t *p) {return u16(p)|(uint32_t)u16(p+2u)<<16;}
static void put16(uint8_t *p,uint16_t n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n) {put16(p,(uint16_t)n);put16(p+2u,(uint16_t)(n>>16));}
static uint32_t physical(uint32_t a) {return a&UINT32_C(0x1fffffff);}
struct source {
    uint8_t *p;size_t bytes;
    struct kui_vmu_native_heap h;
    struct kui_vmu_native_heap_call call;
    uint32_t pool_cell;
    unsigned reads,fail_at;
    uint32_t wrong_address,wrong_value;
};
static uint8_t *at(struct source *s,uint32_t address,unsigned bytes) {
    uint32_t p=physical(address),base=physical(s->h.load);
    CHECK(p>=base && p-base<=s->bytes && bytes<=s->bytes-(p-base));
    return s->p+(p-base);
}
static uint32_t literal(struct source *s,uint32_t pc) {
    uint16_t op=u16(at(s,pc,2u));CHECK((op&0xf000u)==0xd000u);
    return u32(at(s,((pc+4u)&~3u)+4u*(op&255u),4u));
}
static bool read_live(void *context,uint32_t address,uint32_t *value) {
    struct source *s=context;++s->reads;
    uint32_t p=physical(address),alias=address&UINT32_C(0xe0000000);
    CHECK(!(address&3u));CHECK(p>=UINT32_C(0x0c000000) && p<=UINT32_C(0x0cfffffc));
    CHECK(!alias || alias==UINT32_C(0x80000000) || alias==UINT32_C(0xa0000000));
    if(s->fail_at==s->reads)return false;
    if(address==s->wrong_address) {*value=s->wrong_value;return true;}
    if(address==s->h.begin_cell || address==s->h.begin_copy_cell)*value=s->call.entry_begin;
    else if(address==s->h.bytes_cell)*value=s->call.entry_bytes;
    else if(address==s->h.dispatch_cell)*value=s->h.dispatch_table;
    else if(address==s->h.dispatch_table)*value=s->h.allocate;
    else if(address==s->pool_cell)*value=0u;
    else *value=u32(at(s,address,4u));
    return true;
}
static int prepare(struct source *s,uint32_t cost,struct kui_vmu_native_heap_budget *out) {
    s->reads=0u;
    return kui_vmu_native_heap_budget_prepare(s->p,s->bytes,&s->h,&s->call,
        read_live,s,cost,out);
}
static void refused(struct source *s) {
    struct kui_vmu_native_heap_budget out;memset(&out,0xff,sizeof(out));
    CHECK(prepare(s,UINT32_C(0x60020),&out)!=KUI_VMU_NATIVE_HEAP_BUDGET_FOUND);
    const uint8_t zero[sizeof(out)]={0};CHECK(!memcmp(&out,zero,sizeof(out)));
}
static uint8_t *file(const char *name,size_t *bytes) {
    FILE *f=fopen(name,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);
    CHECK(n>0 && n<=0xc00000);rewind(f);uint8_t *p=malloc((size_t)n);CHECK(p);
    CHECK(fread(p,1u,(size_t)n,f)==(size_t)n);CHECK(!fclose(f));*bytes=(size_t)n;return p;
}
static void fixed_mutants(struct source *s,uint32_t begin,unsigned bytes) {
    for(unsigned off=0u;off<bytes;off+=2u) {
        uint8_t *p=at(s,begin+off,2u);uint16_t saved=u16(p);
        /* PC-relative roles are tested separately. Equivalent relocated
         * literal pools are deliberately allowed, not opcode-bit identities. */
        if((saved&0xf000u)==0xd000u || (saved&0xf000u)==0x9000u)continue;
        for(unsigned bit=0u;bit<16u;bit++) {
            put16(p,(uint16_t)(saved^(1u<<bit)));refused(s);++mutants;
        }
        put16(p,saved);
    }
}
static void positive(struct source *s) {
    struct kui_vmu_native_heap_budget b;CHECK(prepare(s,UINT32_C(0x60020),&b)==1);
    CHECK(b.reserve_after-b.reserve_before==UINT32_C(0x60020));
    CHECK(b.child_bytes_before-b.child_bytes_after==UINT32_C(0x60020));
    CHECK(b.child_bytes_after>=64u && !(b.child_bytes_after&31u));
    uint32_t start=s->call.return_pr-0x2cu;
    s->pool_cell=literal(s,start+0x52u);
    CHECK(b.reserve_address==s->h.initializer_slot+4u);
    uint8_t *reserve=at(s,b.reserve_address,4u);
    CHECK(u32(reserve)==b.reserve_before);
    uint32_t source_before=u32(reserve),reads=s->reads;
    const uint32_t costs[]={32u,64u,UINT32_C(0x30020),UINT32_C(0x60020)};
    for(unsigned i=0u;i<sizeof(costs)/sizeof(costs[0]);i++) {
        CHECK(prepare(s,costs[i],&b)==1);
        CHECK(b.reserve_after-b.reserve_before==costs[i]);
        CHECK(b.child_bytes_before-b.child_bytes_after==costs[i]);
        CHECK(u32(reserve)==source_before);
    }
    fixed_mutants(s,start,0x66u);
    uint32_t wrapper=literal(s,start+0x4au),child=literal(s,start+0x5eu);
    fixed_mutants(s,wrapper,10u);fixed_mutants(s,child,0x36u);
    /* A data-looking integer must not authorize overwriting an unrelated
     * executed NOP pair. The reserve stays in its reviewed literal bundle. */
    uint8_t *consumer=at(s,start+0x3au,2u);uint16_t saved_consumer=u16(consumer);
    uint32_t nop=start+0x94u;CHECK(u32(at(s,nop,4u))==UINT32_C(0x00090009));
    put16(consumer,(uint16_t)(0xd400u+((nop-((start+0x3eu)&~3u))/4u)));
    refused(s);++mutants;put16(consumer,saved_consumer);
    uint8_t *extra=at(s,start+0x68u,2u);uint16_t saved_extra=u16(extra);
    uint32_t extra_pc=start+0x68u,slot=s->h.initializer_slot+4u;
    put16(extra,(uint16_t)(0xd400u+((slot-((extra_pc+4u)&~3u))/4u)));
    refused(s);++mutants;
    put16(extra,(uint16_t)(0xc700u+((slot-((extra_pc+4u)&~3u))/4u)));
    refused(s);++mutants;
    put16(extra,(uint16_t)(0xb000u+((slot-(extra_pc+4u))/2u)));
    refused(s);++mutants;put16(extra,saved_extra);
    put32(reserve,0u);refused(s);put32(reserve,UINT32_MAX);refused(s);
    put32(reserve,source_before+32u);CHECK(prepare(s,UINT32_C(0x60020),&b)==1);
    CHECK(b.reserve_before==source_before+32u);
    put32(reserve,source_before);
    CHECK(prepare(s,0u,&b)<0);CHECK(prepare(s,1u,&b)<0);
    CHECK(prepare(s,UINT32_MAX,&b)<0);
    uint8_t prefix[sizeof(b)];memcpy(prefix,s->p,sizeof(prefix));
    CHECK(kui_vmu_native_heap_budget_prepare(s->p,s->bytes,&s->h,&s->call,
        read_live,s,UINT32_C(0x60020),(void *)s->p)<0);
    CHECK(!memcmp(prefix,s->p,sizeof(prefix)));
    for(unsigned fail=1u;fail<=reads;fail++) {
        s->fail_at=fail;refused(s);
    }
    s->fail_at=0u;
    const uint32_t live_addresses[]={s->h.begin_cell,s->h.bytes_cell,s->h.begin_copy_cell,
        s->h.dispatch_cell,s->h.dispatch_table,s->pool_cell,literal(s,start+8u)};
    for(unsigned i=0u;i<sizeof(live_addresses)/sizeof(live_addresses[0]);i++) {
        uint32_t value;CHECK(read_live(s,live_addresses[i],&value));
        s->wrong_address=live_addresses[i];s->wrong_value=value^32u;refused(s);
    }
    s->wrong_address=s->wrong_value=0u;
    CHECK(prepare(s,UINT32_C(0x60020),&b)==1);
}
static void real(const char *image_path,const char *record_path,int expected) {
    struct source s={0};size_t wire_bytes;uint8_t *wire=file(record_path,&wire_bytes);
    s.p=file(image_path,&s.bytes);CHECK(wire_bytes==1024u);
    uint32_t *owner=(uint32_t *)(void *)&s.h,*call=(uint32_t *)(void *)&s.call;
    CHECK(sizeof(s.h)==72u && sizeof(s.call)==20u);
    for(unsigned i=0u;i<18u;i++)owner[i]=u32(wire+608u+4u*i);
    for(unsigned i=0u;i<5u;i++)call[i]=u32(wire+680u+4u*i);
    if(expected==1)s.pool_cell=literal(&s,s.call.return_pr-0x2cu+0x52u);
    struct kui_vmu_native_heap_budget b;
    CHECK(prepare(&s,UINT32_C(0x60020),&b)==expected);
    if(expected==1) {++found;positive(&s);}
    ++originals;free(s.p);free(wire);
}
int main(int argc,char **argv) {
    CHECK(argc>=1 && (argc-1)%3==0);
    for(int i=1;i+2<argc;i+=3)real(argv[i],argv[i+1],atoi(argv[i+2]));
    printf("Native private-pool budget: %u checks, %u fixed-instruction bit mutants refused, %u external originals (%u families)\n",
        checks,mutants,originals,found);
    return 0;
}
