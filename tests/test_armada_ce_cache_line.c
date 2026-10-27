/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_cache_line.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static const uint32_t VA=0x080325e0u,P1=0x8c2345e0u,PAGE=0x0c23417du;
static struct fixture {
    uint32_t tag[4],ram[8],cached[8],tag_reads[4],ram_reads[8],cached_reads[8];
    unsigned calls,fail,mutate_call,mutate_kind,mutate_index;
    uint32_t replacement;
} f;
static unsigned checks;
#define CHECK(c) do {++checks;assert(c);} while(0)
static unsigned bits(uint32_t n) {unsigned count=0;for(;n;n>>=1) count+=n&1u;return count;}
static void prepare(void) {
    memset(&f,0,sizeof(f));
    for(unsigned i=0;i<8u;i++) {f.ram[i]=0x12340000u+i;f.cached[i]=0x56780000u+i;}
}
static bool read32(void *context,uint32_t at,uint32_t *value) {
    CHECK(context==&f && !(at&3u));++f.calls;
    if(f.calls==f.fail) return false;
    if(f.calls==f.mutate_call) {
        CHECK(f.mutate_index<(f.mutate_kind==0?4u:8u));
        (f.mutate_kind==0?f.tag:f.mutate_kind==1?f.ram:f.cached)[f.mutate_index]=f.replacement;
    }
    const uint32_t index=P1&0xfe0u,color=(VA>>12)&3u;
    for(unsigned i=0;i<4u;i++) if(at==(0xf4000000u|index|(i<<12))) {
        ++f.tag_reads[i];*value=f.tag[i];return true;
    }
    for(unsigned i=0;i<8u;i++) {
        if(at==(P1|0x20000000u)+4u*i) {++f.ram_reads[i];*value=f.ram[i];return true;}
        if(at==(0xf5000000u|index|(color<<12))+4u*i) {
            ++f.cached_reads[i];*value=f.cached[i];return true;
        }
    }
    fprintf(stderr,"unexpected cache-line read %08x\n",at);assert(0);return false;
}
static unsigned run(uint32_t page,uint32_t ccr,uint32_t mask,uint32_t out[8]) {
    for(unsigned i=0;i<8u;i++) out[i]=0xa5a5a5a5u;
    unsigned status=kui_armada_ce_cache_line(VA,P1,page,ccr,mask,read32,&f,out);
    if(status) for(unsigned i=0;i<8u;i++) CHECK(out[i]==0);
    return status;
}
static void masks_and_sources(void) {
    uint32_t out[8],color=(VA>>12)&3u,wanted=P1&0x1ffffc00u;
    for(unsigned cached=0;cached<2u;cached++) for(uint32_t mask=1;mask<256u;mask++) {
        prepare();if(cached) f.tag[color]=wanted|3u;
        CHECK(run(PAGE,0x105u,mask,out)==0 && f.calls==8u+2u*bits(mask));
        for(unsigned i=0;i<4u;i++) CHECK(f.tag_reads[i]==2u);
        for(unsigned i=0;i<8u;i++) {
            CHECK(out[i]==((mask&(1u<<i))?(cached?f.cached[i]:f.ram[i]):0));
            CHECK(f.cached_reads[i]==(cached && (mask&(1u<<i))?2u:0u));
            CHECK(f.ram_reads[i]==(!cached && (mask&(1u<<i))?2u:0u));
        }
    }
    for(unsigned alternate=0;alternate<4u;alternate++) {
        prepare();f.tag[alternate]=wanted|3u;
        CHECK(run(PAGE,0x105u,255u,out)==(alternate==color?0u:KUI_ARMADA_CE_RAM_BIND_CACHE));
        prepare();f.tag[alternate]=wanted|3u;
        CHECK(run(PAGE&~8u,0x105u,255u,out)==KUI_ARMADA_CE_RAM_BIND_CACHE);
        /* Clean alternate aliases never override the owner's current VA. */
        prepare();f.tag[alternate]=wanted|1u;
        CHECK(run(PAGE,0x105u,255u,out)==0);
        CHECK(out[0]==(alternate==color?f.cached[0]:f.ram[0]));
    }
}
static void mutations_and_failures(void) {
    uint32_t out[8];
    for(unsigned at=1;at<=24u;at++) {
        prepare();f.fail=at;
        CHECK(run(PAGE,0x105u,255u,out)==KUI_ARMADA_CE_RAM_BIND_READ && f.calls==at);
    }
    for(unsigned i=0;i<4u;i++) {
        prepare();f.mutate_call=13u+i;f.mutate_kind=0;f.mutate_index=i;f.replacement=1u;
        CHECK(run(PAGE,0x105u,255u,out)==KUI_ARMADA_CE_RAM_BIND_CHANGED);
    }
    for(unsigned cached=0;cached<2u;cached++) for(unsigned i=0;i<8u;i++) {
        prepare();if(cached) f.tag[(VA>>12)&3u]=(P1&0x1ffffc00u)|3u;
        f.mutate_call=17u+i;f.mutate_kind=cached?2u:1u;f.mutate_index=i;f.replacement=UINT32_MAX;
        CHECK(run(PAGE,0x105u,255u,out)==KUI_ARMADA_CE_RAM_BIND_CHANGED);
    }
    /* Reserved tag bits are ignored, as in the single-word reference. */
    prepare();f.mutate_call=13u;f.mutate_kind=0;f.replacement=0xe0000000u;
    CHECK(run(PAGE,0x105u,255u,out)==0);
    /* Unrequested words are never touched or published. */
    prepare();f.mutate_call=5u;f.mutate_kind=1;f.mutate_index=7u;f.replacement=UINT32_MAX;
    CHECK(run(PAGE,0x105u,1u,out)==0 && f.ram_reads[7]==0u && out[7]==0u);
}
static void invalid(void) {
    uint32_t out[8];
    static const uint32_t masks[]={0u,256u,UINT32_MAX};
    for(unsigned i=0;i<3u;i++) {prepare();CHECK(run(PAGE,0x105u,masks[i],out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT && !f.calls);}
    prepare();CHECK(run(PAGE,0x125u,1u,out)==KUI_ARMADA_CE_RAM_BIND_CACHE && !f.calls);
    prepare();CHECK(run(PAGE&~1u,0x105u,1u,out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT && !f.calls);
    prepare();CHECK(run(PAGE^0x1000u,0x105u,1u,out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT && !f.calls);
    CHECK(kui_armada_ce_cache_line(VA+4u,P1,PAGE,0x105u,1u,read32,&f,out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
    CHECK(kui_armada_ce_cache_line(VA,P1+4u,PAGE,0x105u,1u,read32,&f,out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
    CHECK(kui_armada_ce_cache_line(VA+32u,P1,PAGE,0x105u,1u,read32,&f,out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
    CHECK(kui_armada_ce_cache_line(VA,0xac2345e0u,PAGE,0x105u,1u,read32,&f,out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
    CHECK(kui_armada_ce_cache_line(0x880325e0u,P1,PAGE,0x105u,1u,read32,&f,out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
    CHECK(kui_armada_ce_cache_line(VA,P1,PAGE,0x105u,1u,NULL,&f,out)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
    CHECK(kui_armada_ce_cache_line(VA,P1,PAGE,0x105u,1u,read32,&f,NULL)==KUI_ARMADA_CE_RAM_BIND_ARGUMENT);
    CHECK(!f.calls);
}
int main(void) {
    masks_and_sources();mutations_and_failures();invalid();
    printf("CE masked cache line: %u checks passed; all255 masks and coherent sources\n",checks);
    return 0;
}
