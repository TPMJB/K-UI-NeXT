/* SPDX-License-Identifier: GPL-3.0-only */
/* No owner executable bytes are embedded. Optional originals exercise the
 * real allocator/caller and mutations of independently discovered metadata. */
#include "kui/vmu_native_program.h"
#include "kui/vmu_native_heap.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Program extent failed at %u\n",__LINE__);exit(1);}} while(0)
static const uint32_t load=UINT32_C(0x8c010000);
static uint16_t u16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static void put16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static void put32(uint8_t *p,uint32_t v) {put16(p,(uint16_t)v);put16(p+2u,(uint16_t)(v>>16));}
static size_t literal_slot(size_t at,uint16_t op) {return ((at+4u)&~3u)+4u*(op&255u);}
static void literal(uint8_t *p,size_t at,unsigned reg,size_t pool,uint32_t value) {
    size_t pc=(at+4u)&~3u;CHECK(pool>=pc && !(pool&3u) && pool-pc<=1020u);
    put16(p+at,(uint16_t)(0xd000u|(reg<<8)|((pool-pc)/4u)));put32(p+pool,value);
}
static void refused(const void *p,size_t n,uint32_t address) {
    size_t got=SIZE_MAX;CHECK(!kui_vmu_native_program_extent(p,n,address,&got));CHECK(got==SIZE_MAX);
}
static void scalar_tests(void) {
    uint8_t p[0x11000]={0};size_t got=0;
    CHECK(kui_vmu_native_program_extent(p,sizeof(p),load,&got) && got==sizeof(p));
    refused(NULL,sizeof(p),load);refused(p,0x2ffu,load);refused(p,0xc00001u,load);
    refused(p,sizeof(p),load+2u);refused(p,sizeof(p),UINT32_C(0xcc010000));
    refused(p,sizeof(p),UINT32_C(0x8c000000));refused(p,sizeof(p),UINT32_C(0x8cfff000));
    CHECK(!kui_vmu_native_program_extent(p,sizeof(p),load,NULL));
    CHECK(!kui_vmu_native_program_extent(p,sizeof(p),load,(size_t *)(void *)p));
    for(unsigned i=0;i<sizeof(p);i++) CHECK(!p[i]);
    CHECK(!kui_vmu_native_program_extent((void *)(UINTPTR_MAX-15u),1024u,load,&got));
    CHECK(!kui_vmu_native_program_extent(p,sizeof(p),load,(size_t *)(UINTPTR_MAX-1u)));
    CHECK(!kui_vmu_native_program_extent((void *)(uintptr_t)UINT32_C(0x8c100000),1024u,load,
        (size_t *)(uintptr_t)UINT32_C(0xac100040)));
    /* An end word in the granule below a boundary starts the arena-tail
     * proof; without the reviewed allocator every tail length keeps the
     * whole file and none is refused. */
    put32(p+0x100u,load+UINT32_C(0x10fc0));
    for(size_t tail=0u;tail<=12u;tail++)
        CHECK(kui_vmu_native_program_extent(p,0x10fe0u+tail,load,&got) && got==0x10fe0u+tail);
    put32(p+0x100u,0u);
    /* A forged format signature cannot alone discard data. Misplaced and
     * truncated IP headers are rejected rather than treated as non-IP data. */
    memcpy(p+sizeof(p)-0x8000u,"SEGA SEGAKATANA SEGA ENTERPRISES",32u);
    refused(p,sizeof(p),load);refused(p,sizeof(p)-1u,load);refused(p,sizeof(p)+0u,load);
    memmove(p+sizeof(p)-0x8000u+1u,p+sizeof(p)-0x8000u,32u);
    refused(p,sizeof(p),load);
}
static void normalized_mutations(uint8_t *p,const uint8_t *original,size_t n,size_t prefix) {
    size_t got;struct kui_vmu_native_heap h;struct kui_vmu_native_heap_call call;
    CHECK(kui_vmu_native_heap_discover(p,n,load,&h)==KUI_VMU_NATIVE_HEAP_FOUND);
    CHECK(kui_vmu_native_heap_call_discover(p,n,&h,&call));
    CHECK((h.initializer&0x1fffffffu)<(load&0x1fffffffu)+prefix);
    CHECK((call.entry_begin&0x1fffffffu)==(load&0x1fffffffu)+prefix+32u);
    CHECK(call.entry_bytes==UINT32_C(0x0d000000)-(call.entry_begin&0x1fffffffu));
    const size_t footer_mutations[]={0u,15u,16u,31u,37u,42u,0x60u,0x6au,0x6fu,
        0x300u,0x306u,0x30cu,0x310u,0x31cu,0x320u,0x324u,0x328u,0x32cu,0x7000u,0x7fffu};
    for(unsigned i=0;i<sizeof(footer_mutations)/sizeof(footer_mutations[0]);i++) {
        memcpy(p,original,n);p[prefix+footer_mutations[i]]^=1u;refused(p,n,load);
    }
    for(size_t cut=1u;cut<=32u;cut++) refused(original,n-cut,load);
    /* Unknown per-title text is never an allowlist: changing its printable
     * title does not alter program lifetime or the canonical boot domain. */
    memcpy(p,original,n);p[prefix+0x80u]='X';
    CHECK(kui_vmu_native_program_extent(p,n,load,&got) && got==prefix);
    const size_t heap_mutations[]={h.initializer-load,h.initializer_slot-load,call.load-load,call.call-load};
    for(unsigned i=0;i<sizeof(heap_mutations)/sizeof(heap_mutations[0]);i++) {
        CHECK(heap_mutations[i]<prefix);memcpy(p,original,n);p[heap_mutations[i]]^=1u;refused(p,n,load);
    }
    /* A second initializer remains ambiguous even if only the first has a
     * startup call; a counterfeit replacement with the old call fails too. */
    size_t old=h.initializer-load,copy=0x1000u;CHECK(old+0x260u<prefix && copy+0x400u<prefix);
    memcpy(p,original,n);memcpy(p+copy,p+old,0x260u);
    size_t pool=literal_slot(copy+0x4eu,u16(p+copy+0x4eu));CHECK(pool+4u<copy+0x260u);
    put32(p+pool,load+(uint32_t)copy+0x300u);
    put32(p+copy+0x300u,load+(uint32_t)copy+0xe2u);
    put32(p+copy+0x304u,load+(uint32_t)copy+0x1b0u);
    refused(p,n,load);
    p[old]^=1u;refused(p,n,load);
    /* Two independently complete startup calls cannot choose the reclaimed
     * arena. The original unique call remains untouched by this insertion. */
    memcpy(p,original,n);literal(p,0x1200u,4u,0x1304u,call.entry_begin);
    literal(p,0x1202u,5u,0x1308u,call.entry_bytes);
    literal(p,0x1204u,3u,0x1300u,h.initializer_target);
    put16(p+0x1206u,0x430bu);put16(p+0x1208u,0x0009u);refused(p,n,load);
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned i=0;i<3u;i++) {
        memcpy(p,original,n);put32(p+0x1000u,aliases[i]|((load&0x1fffffffu)+(uint32_t)prefix+4u));
        refused(p,n,load);
        memcpy(p,original,n);put32(p+0x1000u,aliases[i]|((load&0x1fffffffu)+(uint32_t)prefix));
        refused(p,n,load); /* second copy of the linker boundary */
    }
    const uint16_t branches[]={0xa000u,0xb000u,0x8900u,0x8b00u,0x8d00u,0x8f00u};
    for(unsigned i=0;i<sizeof(branches)/sizeof(branches[0]);i++) {
        memcpy(p,original,n);put16(p+prefix-4u,branches[i]);refused(p,n,load);
    }
    const uint16_t transports[]={0xd000u,0x9000u,0xc700u};
    for(unsigned i=0;i<sizeof(transports)/sizeof(transports[0]);i++) {
        memcpy(p,original,n);put16(p+prefix-4u,transports[i]);refused(p,n,load);
    }
    /* Removing the suffix cannot hide a new firmware exit in the main
     * program: full prefix exit-consumer proof remains mandatory. */
    memcpy(p,original,n);literal(p,0x1000u,3u,0x1020u,UINT32_C(0x8c0000e0));refused(p,n,load);
    memcpy(p,original,n);CHECK(kui_vmu_native_program_extent(p,n,load,&got) && got==prefix);
    CHECK(!memcmp(p,original,n));
}
static void kept(const void *p,size_t n) {
    size_t got=0;CHECK(kui_vmu_native_program_extent(p,n,load,&got) && got==n);
}
/* An original whose own startup arena begins inside its final bytes. Any
 * change that breaks the proof keeps the whole file; nothing is refused. */
static void tail_mutations(uint8_t *p,const uint8_t *original,size_t n,size_t prefix) {
    size_t got,tail=n-prefix;struct kui_vmu_native_heap h;struct kui_vmu_native_heap_call call;
    CHECK(kui_vmu_native_heap_discover(p,n,load,&h)==KUI_VMU_NATIVE_HEAP_FOUND);
    CHECK(kui_vmu_native_heap_call_discover(p,n,&h,&call));
    CHECK((call.entry_begin&0x1fffffffu)==(load&0x1fffffffu)+prefix && tail>=1u && tail<=8u);
    /* Shorter tails over the same boundary stay reclaimed; growing the tail
     * past the initializer's two header words keeps the file whole. */
    for(size_t cut=1u;cut<=tail;cut++)
        CHECK(kui_vmu_native_program_extent(original,n-cut,load,&got) && got==prefix);
    uint8_t *grown=calloc(1u,n+8u);CHECK(grown);memcpy(grown,original,n);
    for(size_t extra=1u;extra<=8u;extra++) {
        CHECK(kui_vmu_native_program_extent(grown,n+extra,load,&got));
        CHECK(got==(tail+extra<=8u?prefix:n+extra));
    }
    free(grown);
    /* An address word naming the tail's interior in any alias, or a second
     * word naming its boundary, keeps the file whole. */
    static const uint8_t zero[16];size_t spare=0u;
    for(size_t at=(prefix-64u)&~(size_t)3u;at>=0x1000u && !spare;at-=4u)
        if(!memcmp(original+at,zero,sizeof(zero))) spare=at;
    CHECK(spare);
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned i=0;i<3u;i++) {
        memcpy(p,original,n);put32(p+spare,aliases[i]|((load&0x1fffffffu)+(uint32_t)n-1u));kept(p,n);
        memcpy(p,original,n);put32(p+spare,aliases[i]|((load&0x1fffffffu)+(uint32_t)prefix));
        CHECK(kui_vmu_native_program_extent(p,n,load,&got) && got==prefix);
        put32(p+spare+4u,aliases[i]|((load&0x1fffffffu)+(uint32_t)prefix));kept(p,n);
    }
    /* Raising the startup's end word one granule moves its arena past the
     * boundary: the file is kept whole. */
    unsigned moved=0u;uint32_t boundary=(load&0x1fffffffu)+(uint32_t)prefix;
    for(size_t at=0;at+4u<=prefix;at+=4u) {
        uint32_t value=(uint32_t)u16(original+at)|(uint32_t)u16(original+at+2u)<<16;
        if((value&0x1fffffffu)>=boundary || (value&0x1fffffffu)+32u<boundary) continue;
        memcpy(p,original,n);put32(p+at,value+32u);
        struct kui_vmu_native_heap h2;struct kui_vmu_native_heap_call c2;
        if(kui_vmu_native_heap_discover(p,n,load,&h2)!=KUI_VMU_NATIVE_HEAP_FOUND ||
           !kui_vmu_native_heap_call_discover(p,n,&h2,&c2) ||
           (c2.entry_begin&0x1fffffffu)==boundary) continue;
        kept(p,n);moved++;
    }
    CHECK(moved);
    /* Branch, literal and MOVA transport into the tail. */
    const uint16_t transports[]={0xa000u,0xb000u,0x8900u,0x8b00u,0x8d00u,0x8f00u,0xd000u,0x9000u,0xc700u};
    for(unsigned i=0;i<sizeof(transports)/sizeof(transports[0]);i++) {
        memcpy(p,original,n);put16(p+prefix-4u,transports[i]);kept(p,n);
    }
    const size_t heap_mutations[]={h.initializer-load,h.initializer_slot-load,call.load-load,call.call-load};
    for(unsigned i=0;i<sizeof(heap_mutations)/sizeof(heap_mutations[0]);i++) {
        CHECK(heap_mutations[i]<prefix);memcpy(p,original,n);p[heap_mutations[i]]^=1u;kept(p,n);
    }
    memcpy(p,original,n);literal(p,0x1000u,3u,0x1020u,UINT32_C(0x8c0000e0));kept(p,n);
    memcpy(p,original,n);CHECK(kui_vmu_native_program_extent(p,n,load,&got) && got==prefix);
    CHECK(!memcmp(p,original,n));
}
static void actual(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);
    CHECK(length>=0x300 && length<=0xc00000);rewind(f);size_t n=(size_t)length;
    uint8_t *p=malloc(n),*original=malloc(n);CHECK(p && original);
    CHECK(fread(p,1,n,f)==n);CHECK(!fclose(f));memcpy(original,p,n);
    size_t prefix=0;CHECK(kui_vmu_native_program_extent(p,n,load,&prefix));
    CHECK(prefix==n || n-prefix==0x8000u || (n-prefix<=8u && !((load+prefix)&31u)));
    CHECK(!memcmp(p,original,n));
    /* A startup arena that begins inside the last eight bytes drops exactly
     * that tail. */
    struct kui_vmu_native_heap h;struct kui_vmu_native_heap_call call;
    if(kui_vmu_native_heap_discover(p,n,load,&h)==KUI_VMU_NATIVE_HEAP_FOUND &&
       kui_vmu_native_heap_call_discover(p,n,&h,&call)) {
        uint32_t begin=call.entry_begin&0x1fffffffu,end=(load&0x1fffffffu)+(uint32_t)n;
        if(begin<end && end-begin<=8u) CHECK(prefix==n-(end-begin));
    }
    /* The fast aligned decoder must retain the exact byte fallback at
     * every host alignment, with no read outside the supplied file. */
    uint8_t *shifted=malloc(n+8u);CHECK(shifted);
    for(unsigned offset=0u;offset<8u;offset++) {
        memcpy(shifted+offset,original,n);size_t same=0u;
        CHECK(kui_vmu_native_program_extent(shifted+offset,n,load,&same));
        CHECK(same==prefix && !memcmp(shifted+offset,original,n));
    }
    free(shifted);
    if(n-prefix==0x8000u) normalized_mutations(p,original,n,prefix);
    else if(prefix!=n) tail_mutations(p,original,n,prefix);
    printf("  Program extent: %s source=%zu program=%zu\n",path,n,prefix);
    free(original);free(p);
}
int main(int argc,char **argv) {
    scalar_tests();for(int i=1;i<argc;i++) actual(argv[i]);
    printf("Native program extent: %u checks passed\n",checks);return 0;
}
