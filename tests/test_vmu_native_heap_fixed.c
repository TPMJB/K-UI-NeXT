/* SPDX-License-Identifier: GPL-3.0-only */
/* Reviewed fixed-arena plans against synthetic images. The digests below
 * are arbitrary test identities supplied by the caller, as the stage and
 * launcher supply the image's own SHA-256. No retail bytes are used. */
#include <assert.h>
#include <stdio.h>
#define BASE UINT32_C(0x8c010000)
#define IMAGE 0x4000u
#define LOAD (BASE+0x200u)
#define SLOT (BASE+0x400u)
#define TARGET (BASE+0x1000u)
#define SENTINEL UINT32_C(0x8c400000)
#define PLAN(tag,begin,bytes) {IMAGE,{tag,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32}, \
    LOAD,LOAD+2u,SLOT,TARGET,begin,bytes}
/* The good plan, then arenas that overlap the image, the allocator's control
 * cells and its dispatch table, are misaligned or are too small. */
#define KUI_VMU_NATIVE_HEAP_TEST_FIXED_PLAN \
    PLAN(0x41,UINT32_C(0x8cf00000),UINT32_C(0x80000)),PLAN(0x42,BASE+0x2000u,UINT32_C(0x80000)), \
    PLAN(0x43,SENTINEL-0x40u,UINT32_C(0x80000)),PLAN(0x44,BASE+0x3000u,UINT32_C(0x80000)), \
    PLAN(0x45,UINT32_C(0x8cf00010),UINT32_C(0x80000)),PLAN(0x46,UINT32_C(0x8cf00000),UINT32_C(0x60)), \
    PLAN(0x47,UINT32_C(0x8cff0000),UINT32_C(0x20000))
#include "../src/core/vmu_native_heap.c"

static unsigned checks,accepted;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint8_t image[IMAGE];
static void put16(size_t at,uint16_t v) {image[at]=(uint8_t)v;image[at+1u]=(uint8_t)(v>>8);}
static void put32(size_t at,uint32_t v) {put16(at,(uint16_t)v);put16(at+2u,(uint16_t)(v>>16));}
static uint8_t digest(uint8_t tag,uint8_t out[32]) {
    for(unsigned i=0;i<32u;i++) out[i]=(uint8_t)(i+1u);
    out[0]=tag;return tag;
}
static void fixture(struct kui_vmu_native_heap *h) {
    memset(image,0,sizeof(image));
    /* MOV.L @(127,PC),R2 reads SLOT; JSR @R2; NOP. */
    put16(LOAD-BASE,0xd27fu);put16(LOAD-BASE+2u,0x420bu);put16(LOAD-BASE+4u,0x0009u);
    put32(SLOT-BASE,TARGET);
    *h=(struct kui_vmu_native_heap){KUI_VMU_NATIVE_HEAP_VERSION,sizeof(*h),KUI_VMU_NATIVE_HEAP_TAIL32,
        KUI_VMU_NATIVE_HEAP_DISPATCH2,IMAGE,BASE,BASE+0x2000u,0u,0u,BASE+0x2000u+0xe2u,BASE+0x2000u+0x1b0u,
        SENTINEL-4u,BASE+0x3000u,SENTINEL,SENTINEL+0x20u,SENTINEL+0x24u,SENTINEL+0x28u,SENTINEL+0x2cu};
}
static bool plan(const struct kui_vmu_native_heap *h,uint8_t tag,struct kui_vmu_native_heap_call *call,
    struct kui_vmu_native_heap_fixed_hook *hook) {
    uint8_t sha[32];digest(tag,sha);
    struct kui_vmu_native_heap_call before;memset(call,0xa5,sizeof(*call));before=*call;
    bool result=kui_vmu_native_heap_fixed_plan(image,IMAGE,sha,h,call,hook);
    if(!result) CHECK(!memcmp(call,&before,sizeof(before)));
    return result;
}
int main(void) {
    struct kui_vmu_native_heap h;struct kui_vmu_native_heap_call call;
    struct kui_vmu_native_heap_fixed_hook hook;
    fixture(&h);CHECK(metadata(&h));
    CHECK(plan(&h,0x41,&call,&hook));
    CHECK(call.load==LOAD && call.call==LOAD+2u && call.return_pr==LOAD+6u &&
        call.entry_begin==UINT32_C(0x8cf00000) && call.entry_bytes==UINT32_C(0x80000));
    CHECK(hook.slot==SLOT && hook.target==TARGET);
    CHECK(plan(&h,0x41,&call,NULL));
    /* Only the exact image identity selects a plan. */
    CHECK(!plan(&h,0x40,&call,&hook));CHECK(!plan(&h,0x48,&call,&hook));
    uint8_t sha[32];digest(0x41,sha);sha[31]^=1u;
    CHECK(!kui_vmu_native_heap_fixed_plan(image,IMAGE,sha,&h,&call,&hook));
    digest(0x41,sha);
    CHECK(!kui_vmu_native_heap_fixed_plan(image,IMAGE-2u,sha,&h,&call,&hook));
    /* Each bad arena is refused for its own image. */
    for(uint8_t tag=0x42;tag<=0x47;tag++) CHECK(!plan(&h,tag,&call,&hook));
    /* An allocator that a pointer already names takes the initializer path. */
    struct kui_vmu_native_heap named=h;named.initializer_slot=SLOT;named.initializer_target=named.initializer;
    CHECK(metadata(&named) && !plan(&named,0x41,&call,&hook));
    named=h;named.family=2u;CHECK(!plan(&named,0x41,&call,&hook));
    named=h;named.image_bytes=IMAGE-0x10u;CHECK(!plan(&named,0x41,&call,&hook));
    /* The literal must still name the planned target. */
    put32(SLOT-BASE,TARGET+2u);CHECK(!plan(&h,0x41,&call,&hook));fixture(&h);
    /* One consumer, and no immutable address-of-slot word anywhere. */
    put16(0x100u,0xd3bfu);CHECK(!plan(&h,0x41,&call,&hook));fixture(&h);  /* (0x104+0x2fc) reads SLOT */
    CHECK(BASE+((0x100u+4u)&~3u)+4u*0xbfu==SLOT);
    put32(0x800u,SLOT);CHECK(!plan(&h,0x41,&call,&hook));fixture(&h);
    put32(0x800u,(SLOT&UINT32_C(0x1fffffff))|UINT32_C(0xa0000000));CHECK(!plan(&h,0x41,&call,&hook));fixture(&h);
    put32(0x802u,SLOT);CHECK(plan(&h,0x41,&call,&hook));fixture(&h);     /* Unaligned: not a word. */
    /* Every 16-bit value at each of the three hooked instructions: only the
     * original sequence is a borrowed MOV.L/JSR/NOP call through the slot. */
    for(unsigned position=0;position<3u;position++) {
        size_t at=LOAD-BASE+2u*position;uint16_t original=(uint16_t)(image[at]|image[at+1u]<<8);
        for(unsigned value=0;value<=UINT16_MAX;value++) {
            put16(at,(uint16_t)value);
            if(plan(&h,0x41,&call,&hook)) {++accepted;CHECK(value==original);}
        }
        put16(at,original);
    }
    CHECK(accepted==3u);
    /* Outputs may not overlap the image or each other. */
    CHECK(!kui_vmu_native_heap_fixed_plan(image,IMAGE,sha,&h,(void *)image,&hook));
    CHECK(!kui_vmu_native_heap_fixed_plan(image,IMAGE,sha,&h,&call,(void *)(image+64u)));
    CHECK(!kui_vmu_native_heap_fixed_plan(image,IMAGE,sha,&h,&call,(void *)&call));
    CHECK(!kui_vmu_native_heap_fixed_plan(NULL,IMAGE,sha,&h,&call,&hook));
    CHECK(!kui_vmu_native_heap_fixed_plan(image,IMAGE,NULL,&h,&call,&hook));
    CHECK(!kui_vmu_native_heap_fixed_plan(image,IMAGE,sha,NULL,&call,&hook));
    CHECK(!kui_vmu_native_heap_fixed_plan(image,IMAGE,sha,&h,NULL,&hook));
    printf("Native fixed arena plan: %u checks; 196608 instruction mutations, only the reviewed call admitted\n",checks);
    return 0;
}
