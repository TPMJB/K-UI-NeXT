/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent widened interval oracle for the complete bottom transaction. */
#include "kui/vmu_native_stage_placement.h"
#include "kui/vmu_native_background.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"Bottom stage placement line %u\n",__LINE__);exit(1);}} while(0)
static const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
static uint64_t phys(uint32_t at) {return at&UINT32_C(0x1fffffff);}
static bool alias(uint32_t at) {
    return (at&UINT32_C(0xe0000000))==0u ||
        (at&UINT32_C(0xe0000000))==UINT32_C(0x80000000) ||
        (at&UINT32_C(0xe0000000))==UINT32_C(0xa0000000);
}
static bool intersects(uint64_t at,uint64_t bytes,uint64_t other,uint64_t end) {
    return at<end && other<at+bytes;
}
static bool oracle(const struct kui_vmu_native_heap *h,uint32_t image,
    uint32_t stage,uint32_t end,uint32_t arena,uint32_t bytes) {
    uint64_t s=phys(stage),z=phys(end),b=phys(arena),e=b+bytes;
    const uint64_t ram=UINT64_C(0x0c010000),limit=UINT64_C(0x0d000000);
    uint64_t retained=KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES+32u;
    if(!h || !image || image>0xc00000u || !alias(stage) || !alias(end) ||
       ((stage^end)&UINT32_C(0xe0000000)) || s<ram || z<=s || z>limit ||
       intersects(ram,image,s,z) || !alias(arena) || (arena&31u) || (bytes&31u) ||
       b<ram || b>=limit || bytes<retained+64u || e>limit ||
       intersects(ram,image,b,e)) return false;
    /* These are concrete write intervals, not the placeholder's unused
     * payload: retained header/payload, placeholder header, allocator state. */
    uint32_t at[4]={arena,arena+(uint32_t)retained};
    uint64_t count[4]={retained,32u};unsigned intervals;
    if(h->family==KUI_VMU_NATIVE_HEAP_TAIL32 && h->layout==KUI_VMU_NATIVE_HEAP_DISPATCH2) {
        if(h->sentinel<4u) return false;
        at[2]=h->sentinel-4u;count[2]=0x34u;at[3]=h->dispatch_table;count[3]=8u;intervals=4u;
    } else if(h->family==KUI_VMU_NATIVE_HEAP_DIRECT32 && h->layout==KUI_VMU_NATIVE_HEAP_DIRECT) {
        if(h->dispatch_cell || h->dispatch_table) return false;
        at[2]=h->sentinel;count[2]=0x30u;intervals=3u;
    } else return false;
    for(unsigned i=0;i<intervals;i++) {
        uint64_t a=phys(at[i]);
        if(!alias(at[i]) || a<ram || a+count[i]>limit || intersects(a,count[i],s,z)) return false;
    }
    return true;
}
static struct kui_vmu_native_heap fixture(void) {
    struct kui_vmu_native_heap h={0};h.sentinel=UINT32_C(0x8c280040);
    h.family=KUI_VMU_NATIVE_HEAP_TAIL32;h.layout=KUI_VMU_NATIVE_HEAP_DISPATCH2;
    h.dispatch_table=UINT32_C(0x8c170000);return h;
}
static void compare(const struct kui_vmu_native_heap *h,uint32_t image,
    uint32_t stage,uint32_t end,uint32_t arena,uint32_t bytes) {
    CHECK(kui_vmu_native_stage_bottom_allocation_safe(h,image,stage,end,arena,bytes)==
        oracle(h,image,stage,end,arena,bytes));
}
static void direct32_cases(void) {
    struct kui_vmu_native_heap h=fixture();h.family=KUI_VMU_NATIVE_HEAP_DIRECT32;
    h.layout=KUI_VMU_NATIVE_HEAP_DIRECT;h.dispatch_cell=h.dispatch_table=0u;
    const uint32_t arena=UINT32_C(0x0c300000),bytes=UINT32_C(0x00d00000),image=0x100000u;
    const uint32_t stage=UINT32_C(0x0ce00000),end=UINT32_C(0x0cea0000);
    const uint32_t retained=KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES+32u;
    for(unsigned a=0;a<3u;a++) for(unsigned b=0;b<3u;b++) for(unsigned c=0;c<3u;c++) {
        for(int delta=-64;delta<=64;delta++) {
            h.sentinel=(stage+(uint32_t)delta)|aliases[c];
            compare(&h,image,stage|aliases[a],end|aliases[a],arena|aliases[b],bytes);
            h.sentinel=(end+(uint32_t)delta)|aliases[c];
            compare(&h,image,stage|aliases[a],end|aliases[a],arena|aliases[b],bytes);
        }
        h.sentinel=UINT32_C(0x0c280040)|aliases[c];
        for(int delta=-64;delta<=64;delta++) {
            uint32_t at=arena+retained+(uint32_t)delta;
            compare(&h,image,at|aliases[a],(at+0x1000u)|aliases[a],arena|aliases[b],bytes);
        }
    }
    h.sentinel=end;
    CHECK(kui_vmu_native_stage_bottom_allocation_safe(&h,image,stage,end,arena,bytes));
    h.sentinel=end-1u;CHECK(!kui_vmu_native_stage_bottom_allocation_safe(&h,image,stage,end,arena,bytes));
    h.sentinel=stage-0x30u;CHECK(kui_vmu_native_stage_bottom_allocation_safe(&h,image,stage,end,arena,bytes));
    h.sentinel++;CHECK(!kui_vmu_native_stage_bottom_allocation_safe(&h,image,stage,end,arena,bytes));
    h.sentinel=UINT32_C(0x0c280040);
    for(unsigned family=0u;family<=5u;family++) for(unsigned layout=0u;layout<=4u;layout++) {
        struct kui_vmu_native_heap bad=h;bad.family=family;bad.layout=layout;
        compare(&bad,image,stage,end,arena,bytes);
    }
    for(unsigned which=0u;which<2u;which++) {
        struct kui_vmu_native_heap bad=h;
        if(which) bad.dispatch_cell=UINT32_C(0x8ce00000);else bad.dispatch_table=UINT32_C(0x8ce00000);
        CHECK(!kui_vmu_native_stage_bottom_allocation_safe(&bad,image,stage,end,arena,bytes));
    }
    CHECK(!kui_vmu_native_stage_bottom_allocation_safe(&h,image,stage,end,UINT32_C(0x0c10ffe0),bytes));
}
int main(void) {
    direct32_cases();
    struct kui_vmu_native_heap h=fixture();
    const uint32_t begin=UINT32_C(0x0c300000),bytes=UINT32_C(0x00d00000),image=0x100000u;
    const uint32_t retained=KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES+32u;
    const uint32_t stage=UINT32_C(0x0ce00000),end=UINT32_C(0x0cea0000);
    /* The temporary placeholder contains the stage, but none of its payload
     * is touched by the exact allocator transaction. */
    CHECK(kui_vmu_native_stage_bottom_allocation_safe(&h,image,stage,end,begin,bytes));
    /* Checking only the retained lease would miss this adjacent metadata. */
    CHECK(!kui_vmu_native_stage_bottom_allocation_safe(&h,image,
        begin+retained,begin+retained+0x1000u,begin,bytes));
    CHECK(kui_vmu_native_stage_bottom_allocation_safe(&h,image,
        begin+retained+32u,begin+retained+0x1000u,begin,bytes));
    for(unsigned a=0;a<3u;a++) for(unsigned b=0;b<3u;b++) for(unsigned c=0;c<3u;c++) {
        h=fixture();h.sentinel=(uint32_t)phys(h.sentinel)|aliases[c];
        h.dispatch_table=(uint32_t)phys(h.dispatch_table)|aliases[2u-c];
        compare(&h,image,stage|aliases[a],end|aliases[a],begin|aliases[b],bytes);
        for(int d=-96;d<=96;d++) {
            uint32_t boundary=begin+retained+(uint32_t)d;
            compare(&h,image,boundary|aliases[a],(boundary+0x1000u)|aliases[a],begin|aliases[b],bytes);
            compare(&h,image,(begin-0x1000u)|aliases[a],boundary|aliases[a],begin|aliases[b],bytes);
        }
    }
    const uint32_t values[]={0u,1u,31u,32u,63u,64u,95u,96u,0x100000u,
        UINT32_C(0x0c010000),UINT32_C(0x0c300000),UINT32_C(0x0ce00000),
        UINT32_C(0x0d000000),UINT32_C(0x20000000),UINT32_MAX};
    h=fixture();
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++)
        for(unsigned j=0;j<sizeof(values)/sizeof(values[0]);j++) {
            compare(&h,image,stage,end,values[i],values[j]);
            compare(&h,values[i],stage,values[j],begin,bytes);
            compare(&h,image,values[i],end,begin,values[j]);
            struct kui_vmu_native_heap bad=h;bad.sentinel=values[i];bad.dispatch_table=values[j];
            compare(&bad,image,stage,end,begin,bytes);
        }
    CHECK(!kui_vmu_native_stage_bottom_allocation_safe(NULL,image,stage,end,begin,bytes));
    printf("Bottom transaction stage placement: %u checks passed (lease %u bytes)\n",
        checks,(unsigned)KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES);
    return 0;
}
