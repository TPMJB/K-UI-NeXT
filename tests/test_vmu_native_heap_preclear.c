/* SPDX-License-Identifier: GPL-3.0-only */
/* Owner-supplied originals exercise the cold pre-initializer clear contract. */
#include "kui/vmu_native_heap.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks,mutants;
#define CHECK(x) do {++checks;assert(x);} while(0)
static struct kui_vmu_native_heap_preclear untouched;
static int plan(const uint8_t *data,size_t n,const uint8_t digest[32],
    const struct kui_vmu_native_heap *heap,const struct kui_vmu_native_heap_call *call) {
    struct kui_vmu_native_heap_preclear out=untouched;
    int result=kui_vmu_native_heap_preclear_plan(data,n,digest,heap,call,&out);
    if(result!=1) CHECK(!memcmp(&out,&untouched,sizeof(out)));
    return result;
}
static uint8_t *source(const char *path,size_t *size,uint8_t digest[32],
    struct kui_vmu_native_heap *heap,struct kui_vmu_native_heap_call *call) {
    FILE *file=fopen(path,"rb");CHECK(file && !fseek(file,0,SEEK_END));
    long bytes=ftell(file);CHECK(bytes>0 && bytes<=0xc00000 && !fseek(file,0,SEEK_SET));
    uint8_t *data=malloc((size_t)bytes);CHECK(data);
    CHECK(fread(data,1,(size_t)bytes,file)==(size_t)bytes && !fclose(file));*size=(size_t)bytes;
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,data,*size);
    kui_sha256_digest(&hash,digest);
    CHECK(kui_vmu_native_heap_discover(data,*size,UINT32_C(0x8c010000),heap)==KUI_VMU_NATIVE_HEAP_FOUND);
    CHECK(kui_vmu_native_heap_call_discover(data,*size,heap,call));return data;
}
static void expected_refusal(uint8_t *data,size_t n,const uint8_t digest[32],
    const struct kui_vmu_native_heap *heap,const struct kui_vmu_native_heap_call *call,
    size_t offset,size_t bytes) {
    CHECK(offset<=n && bytes<=n-offset);
    for(size_t bit=0;bit<bytes*8u;bit++) {
        data[offset+bit/8u]^=(uint8_t)(1u<<(bit&7u));
        CHECK(plan(data,n,digest,heap,call)==-1);
        data[offset+bit/8u]^=(uint8_t)(1u<<(bit&7u));++mutants;
    }
}
static void time_stalkers(const char *path) {
    size_t n;uint8_t digest[32];struct kui_vmu_native_heap heap;
    struct kui_vmu_native_heap_call call;
    uint8_t *data=source(path,&n,digest,&heap,&call);
    struct kui_vmu_native_heap_preclear out;
    CHECK(kui_vmu_native_heap_preclear_plan(data,n,digest,&heap,&call,&out)==1);
    CHECK(out.slot==UINT32_C(0x8c0102d0) && out.target==UINT32_C(0x8c01c6e8) &&
        out.return_pr==UINT32_C(0x8c010230) && out.begin==UINT32_C(0x8c3b0000) &&
        out.bytes==UINT32_C(0xc50000));
    /* The digest parameter is already independently bound to the original
     * immutable image by the caller. Even with that digest unchanged, every
     * executed path/helper bit and its four literal roles must still match. */
    expected_refusal(data,n,digest,&heap,&call,0x226u,20u);
    expected_refusal(data,n,digest,&heap,&call,0xc6e8u,24u);
    expected_refusal(data,n,digest,&heap,&call,0x2c8u,16u);
    expected_refusal(data,n,digest,&heap,&call,0x1598cu,20u);
    for(unsigned bit=0;bit<256u;bit++) {
        digest[bit/8u]^=(uint8_t)(1u<<(bit&7u));
        CHECK(plan(data,n,digest,&heap,&call)==-1);
        digest[bit/8u]^=(uint8_t)(1u<<(bit&7u));++mutants;
    }
    for(unsigned field=0;field<sizeof(heap)/4u;field++)
        for(unsigned bit=0;bit<32u;bit++) {
            struct kui_vmu_native_heap changed=heap;
            uint32_t *words=(uint32_t *)(void *)&changed;
            words[field]^=UINT32_C(1)<<bit;
            CHECK(plan(data,n,digest,&changed,&call)==-1);++mutants;
        }
    for(unsigned field=0;field<sizeof(call)/4u;field++) for(unsigned bit=0;bit<32u;bit++) {
        struct kui_vmu_native_heap_call changed=call;
        uint32_t *words=(uint32_t *)(void *)&changed;words[field]^=UINT32_C(1)<<bit;
        CHECK(plan(data,n,digest,&heap,&changed)==-1);++mutants;
    }
    CHECK(plan(NULL,n,digest,&heap,&call)==-1);
    CHECK(plan(data,n,NULL,&heap,&call)==-1);
    CHECK(plan(data,n,digest,NULL,&call)==-1);
    CHECK(plan(data,n,digest,&heap,NULL)==-1);
    CHECK(kui_vmu_native_heap_preclear_plan(data,n,digest,&heap,&call,NULL)==-1);
    CHECK(plan(data,n-1u,digest,&heap,&call)==-1);
    CHECK(plan(data,n+1u,digest,&heap,&call)==-1);
    CHECK(plan(data,0u,digest,&heap,&call)==-1);
    CHECK(plan((const void *)(UINTPTR_MAX-16u),n,digest,&heap,&call)==-1);
    /* A second encoded PC-relative consumer or an address-of-slot word
     * makes borrowing this literal unsafe, even outside the short path. */
    size_t at=0x210u;uint16_t old;memcpy(&old,data+at,2u);
    uint16_t extra=(uint16_t)(0xd100u|((0x2d0u-((at+4u)&~3u))/4u));
    memcpy(data+at,&extra,2u);CHECK(plan(data,n,digest,&heap,&call)==-1);
    memcpy(data+at,&old,2u);
    for(unsigned alias=0;alias<3u;alias++) {
        uint32_t word=UINT32_C(0x0c0102d0)|(alias==0u?0u:alias==1u?UINT32_C(0x80000000):UINT32_C(0xa0000000));
        uint32_t prior;memcpy(&prior,data+0x400u,4u);memcpy(data+0x400u,&word,4u);
        CHECK(plan(data,n,digest,&heap,&call)==-1);memcpy(data+0x400u,&prior,4u);
    }
    uint8_t *before=malloc(n);CHECK(before);memcpy(before,data,n);
    CHECK(kui_vmu_native_heap_preclear_plan(data,n,digest,&heap,&call,
        (struct kui_vmu_native_heap_preclear *)(void *)(data+0x400u))==-1);
    CHECK(!memcmp(before,data,n));free(before);
    struct kui_vmu_native_heap saved=heap;
    CHECK(kui_vmu_native_heap_preclear_plan(data,n,digest,&heap,&call,
        (struct kui_vmu_native_heap_preclear *)(void *)&heap)==-1);
    CHECK(!memcmp(&saved,&heap,sizeof(heap)));
    struct kui_vmu_native_heap_call saved_call=call;
    CHECK(kui_vmu_native_heap_preclear_plan(data,n,digest,&heap,&call,
        (struct kui_vmu_native_heap_preclear *)(void *)&call)==-1);
    CHECK(!memcmp(&saved_call,&call,sizeof(call)));
    uint8_t saved_digest[32];memcpy(saved_digest,digest,32u);
    CHECK(kui_vmu_native_heap_preclear_plan(data,n,digest,&heap,&call,
        (struct kui_vmu_native_heap_preclear *)(void *)digest)==-1);
    CHECK(!memcmp(saved_digest,digest,32u));
    CHECK(plan(data,n,digest,&heap,&call)==1);free(data);
}
int main(int argc,char **argv) {
    CHECK(argc==3);memset(&untouched,0xa5,sizeof(untouched));time_stalkers(argv[1]);
    size_t n;uint8_t digest[32];struct kui_vmu_native_heap heap;struct kui_vmu_native_heap_call call;
    uint8_t *data=source(argv[2],&n,digest,&heap,&call);
    CHECK(plan(data,n,digest,&heap,&call)==0);free(data);
    printf("Original preheap clear contract: %u checks, %u bit mutants refused; Time Stalkers exact, EGG unchanged\n",checks,mutants);
    return 0;
}
