/* SPDX-License-Identifier: GPL-3.0-only */
/* Geometry oracle and the original top-of-RAM rejection, without game code. */
#include "kui/vmu_native_stage_placement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"Stage placement failed at line %u\n",__LINE__);exit(1);}} while(0)
static const uint32_t areas[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
static const uint32_t first=UINT32_C(0x0c010000),end_ram=UINT32_C(0x0d000000);
static uint32_t phys(uint32_t at) {return at&UINT32_C(0x1fffffff);}
static bool old_temporary_overlap(uint32_t address,uint32_t bytes,uint32_t stage_end) {
    uint32_t p=phys(address),area=address&UINT32_C(0xe0000000);
    if((area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000)) ||
       !bytes || p<first || p>=end_ram || bytes>end_ram-p) return true;
    return (p<stage_end && UINT32_C(0x0ce00000)<p+bytes) || p+bytes>UINT32_C(0x0cfe0000);
}
static bool old_safe(const struct kui_vmu_native_heap *h,uint32_t image,uint32_t stage_end,
    uint32_t begin,uint32_t bytes) {
    if(bytes<0x30040u || old_temporary_overlap(begin,64u,stage_end) ||
       old_temporary_overlap(h->sentinel-4u,0x34u,stage_end) ||
       old_temporary_overlap(h->dispatch_table,8u,stage_end)) return false;
    uint32_t p=phys(begin),area=begin&UINT32_C(0xe0000000);
    if(bytes>end_ram-p || (area!=0u && area!=UINT32_C(0x80000000) && area!=UINT32_C(0xa0000000))) return false;
    return !old_temporary_overlap(begin+bytes-0x30020u,0x30020u,stage_end) &&
        !(p<first+image && first<p+bytes);
}
static bool good_alias(uint32_t at) {
    uint32_t area=at&UINT32_C(0xe0000000);
    return area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000);
}
/* Independent widened interval oracle: enumerate all writes protected by this
 * API, then reject an intersecting or malformed interval. It has no blanket
 * high-stack reservation and no knowledge of worker load addresses. */
static bool oracle(const struct kui_vmu_native_heap *h,uint32_t image,uint32_t stage_begin,
    uint32_t stage_end,uint32_t arena,uint32_t bytes) {
    if(!h || !image || image>0xc00000u || !good_alias(stage_begin) || !good_alias(stage_end) ||
       ((stage_begin^stage_end)&UINT32_C(0xe0000000)) || !good_alias(arena)) return false;
    uint64_t stage=phys(stage_begin),limit=phys(stage_end),begin=phys(arena),finish=begin+bytes;
    if(stage<first || stage>=end_ram || limit<=stage || limit>end_ram ||
       (stage<(uint64_t)first+image && first<limit) ||
       bytes<0x30040u || begin<first || begin>=end_ram || finish>end_ram ||
       (begin<(uint64_t)first+image && first<finish) || h->sentinel<4u) return false;
    const uint32_t address[]={arena,h->sentinel-4u,h->dispatch_table};
    const uint32_t length[]={64u,0x34u,8u};
    for(unsigned i=0;i<3u;i++) {
        uint64_t at=phys(address[i]),end=at+length[i];
        if(!good_alias(address[i]) || at<first || at>=end_ram || end>end_ram ||
           (at<limit && stage<end)) return false;
    }
    return !(finish-0x30020u<limit && stage<finish);
}
static struct kui_vmu_native_heap fixture(void) {
    struct kui_vmu_native_heap h={0};h.sentinel=UINT32_C(0x8c280040);
    h.dispatch_table=UINT32_C(0x8c170000);return h;
}
static void compare(const struct kui_vmu_native_heap *h,uint32_t image,uint32_t stage,
    uint32_t stage_end,uint32_t begin,uint32_t bytes) {
    CHECK(kui_vmu_native_stage_allocation_safe(h,image,stage,stage_end,begin,bytes)==
        oracle(h,image,stage,stage_end,begin,bytes));
}
static void direct_cases(void) {
    struct kui_vmu_native_heap h=fixture();const uint32_t stage=UINT32_C(0x8ce00000),limit=UINT32_C(0x8ce40000);
    CHECK(!old_safe(&h,1578116u,phys(limit),UINT32_C(0x8cc00000),0x400000u));
    CHECK(kui_vmu_native_stage_allocation_safe(&h,1578116u,stage,limit,UINT32_C(0x8cc00000),0x400000u));
    CHECK(!old_safe(&h,5460080u,phys(limit),UINT32_C(0x8c5e9ba0),0xa16460u));
    CHECK(kui_vmu_native_stage_allocation_safe(&h,5460080u,stage,limit,UINT32_C(0x8c5e9ba0),0xa16460u));
    /* The entire 192KiB allocation and its header remain excluded, including
     * the 64KiB not occupied by the linked 128KiB worker. */
    CHECK(!kui_vmu_native_stage_allocation_safe(&h,1578116u,UINT32_C(0x8cff1000),UINT32_C(0x8cff2000),UINT32_C(0x8cc00000),0x400000u));
    CHECK(!kui_vmu_native_stage_allocation_safe(NULL,1578116u,stage,limit,UINT32_C(0x8cc00000),0x400000u));
    for(unsigned i=0;i<3u;i++) for(unsigned j=0;j<3u;j++) for(unsigned k=0;k<3u;k++) {
        h=fixture();h.sentinel=phys(h.sentinel)|areas[k];h.dispatch_table=phys(h.dispatch_table)|areas[j];
        CHECK(kui_vmu_native_stage_allocation_safe(&h,1578116u,phys(stage)|areas[i],phys(limit)|areas[i],UINT32_C(0x0cc00000)|areas[j],0x400000u));
    }
}
static void boundaries(void) {
    struct kui_vmu_native_heap original=fixture();
    const uint32_t stage=UINT32_C(0x0ce00000),limit=UINT32_C(0x0ce40000);
    /* Sweep every byte where the arena's initial 64 bytes enter/leave stage.
     * A minimal valid arena keeps the tail just after that initial range. */
    for(uint32_t at=stage-128u;at<=limit+128u;at++) for(unsigned alias=0;alias<3u;alias++)
        compare(&original,0x100000u,stage|areas[alias],limit|areas[alias],at|areas[2u-alias],0x30040u);
    const uint32_t boundaries[]={stage,limit};
    for(unsigned boundary=0;boundary<2u;boundary++) for(int delta=-96;delta<=96;delta++) {
        uint32_t at=boundaries[boundary]+(uint32_t)delta;
        for(unsigned alias=0;alias<3u;alias++) {
            struct kui_vmu_native_heap h=original;h.sentinel=(at+4u)|areas[alias];
            compare(&h,0x100000u,stage|areas[1],limit|areas[1],UINT32_C(0x0cc00000)|areas[alias],0x400000u);
            h=original;h.dispatch_table=at|areas[alias];
            compare(&h,0x100000u,stage|areas[1],limit|areas[1],UINT32_C(0x0cc00000)|areas[alias],0x400000u);
        }
    }
    /* Slide the predicted tail across both linked-stage bounds. */
    for(unsigned boundary=0;boundary<2u;boundary++) for(int delta=-96;delta<=96;delta++) {
        uint32_t tail=boundaries[boundary]+(uint32_t)delta;
        uint32_t begin=UINT32_C(0x0cc00000),bytes=tail+0x30020u-begin;
        compare(&original,0x100000u,stage,limit,begin,bytes);
    }
    /* Enlarging linked BSS to include one more active byte must refuse. */
    CHECK(kui_vmu_native_stage_allocation_safe(&original,0x100000u,stage,limit,limit,0x30040u));
    CHECK(!kui_vmu_native_stage_allocation_safe(&original,0x100000u,stage,limit+1u,limit,0x30040u));
    /* A newly linked installer stack extends beyond the old initialized
     * stage's end. Passing only the old bound would miss these intersections. */
    const uint32_t old_bss=UINT32_C(0x0ce29b00),stack_byte=UINT32_C(0x0ce30000);
    struct kui_vmu_native_heap h=original;h.sentinel=stack_byte+4u;
    CHECK(kui_vmu_native_stage_allocation_safe(&h,0x100000u,stage,old_bss,UINT32_C(0x0cc00000),0x400000u));
    CHECK(!kui_vmu_native_stage_allocation_safe(&h,0x100000u,stage,limit,UINT32_C(0x0cc00000),0x400000u));
    h=original;h.dispatch_table=stack_byte;
    CHECK(kui_vmu_native_stage_allocation_safe(&h,0x100000u,stage,old_bss,UINT32_C(0x0cc00000),0x400000u));
    CHECK(!kui_vmu_native_stage_allocation_safe(&h,0x100000u,stage,limit,UINT32_C(0x0cc00000),0x400000u));
    CHECK(kui_vmu_native_stage_allocation_safe(&original,0x100000u,stage,old_bss,stack_byte,0x30040u));
    CHECK(!kui_vmu_native_stage_allocation_safe(&original,0x100000u,stage,limit,stack_byte,0x30040u));
    uint32_t arena=UINT32_C(0x0cc00000),bytes=stack_byte+0x30020u-arena;
    CHECK(kui_vmu_native_stage_allocation_safe(&original,0x100000u,stage,old_bss,arena,bytes));
    CHECK(!kui_vmu_native_stage_allocation_safe(&original,0x100000u,stage,limit,arena,bytes));
}
static void malformed(void) {
    const uint32_t starts[]={0u,1u,UINT32_C(0x0c000000),UINT32_C(0x0c00ffff),UINT32_C(0x0c010000),
        UINT32_C(0x0c010001),UINT32_C(0x0cc00000),UINT32_C(0x0cfb0000),UINT32_C(0x0cfcffc0),
        UINT32_C(0x0cfcffe0),UINT32_C(0x0cfffffc),UINT32_C(0x0d000000),UINT32_C(0x1fffffff),UINT32_MAX};
    const uint32_t lengths[]={0u,1u,31u,32u,63u,64u,0x3001fu,0x30020u,0x3003fu,0x30040u,
        0x30041u,0x400000u,0x1000000u,UINT32_MAX};
    const uint32_t wrong[]={UINT32_C(0x20000000),UINT32_C(0x40000000),UINT32_C(0x60000000),
        UINT32_C(0xc0000000),UINT32_C(0xe0000000)};
    struct kui_vmu_native_heap h=fixture();
    for(unsigned a=0;a<sizeof(starts)/sizeof(starts[0]);a++)
        for(unsigned n=0;n<sizeof(lengths)/sizeof(lengths[0]);n++) for(unsigned area=0;area<3u;area++)
            compare(&h,1578116u,UINT32_C(0x8ce00000),UINT32_C(0x8ce40000),starts[a]|areas[area],lengths[n]);
    for(unsigned a=0;a<sizeof(starts)/sizeof(starts[0]);a++) {
        compare(&h,1578116u,starts[a],UINT32_C(0x0ce40000),UINT32_C(0x0cc00000),0x400000u);
        compare(&h,1578116u,UINT32_C(0x0ce00000),starts[a],UINT32_C(0x0cc00000),0x400000u);
        struct kui_vmu_native_heap changed=h;changed.sentinel=starts[a];
        compare(&changed,1578116u,UINT32_C(0x8ce00000),UINT32_C(0x8ce40000),UINT32_C(0x8cc00000),0x400000u);
        changed=h;changed.dispatch_table=starts[a];
        compare(&changed,1578116u,UINT32_C(0x8ce00000),UINT32_C(0x8ce40000),UINT32_C(0x8cc00000),0x400000u);
    }
    for(unsigned i=0;i<sizeof(wrong)/sizeof(wrong[0]);i++) {
        compare(&h,1578116u,UINT32_C(0x0ce00000)|wrong[i],UINT32_C(0x0ce40000)|wrong[i],UINT32_C(0x8cc00000),0x400000u);
        compare(&h,1578116u,UINT32_C(0x8ce00000),UINT32_C(0x8ce40000),UINT32_C(0x0cc00000)|wrong[i],0x400000u);
        struct kui_vmu_native_heap changed=h;changed.sentinel=UINT32_C(0x0c280040)|wrong[i];
        compare(&changed,1578116u,UINT32_C(0x8ce00000),UINT32_C(0x8ce40000),UINT32_C(0x8cc00000),0x400000u);
        changed=h;changed.dispatch_table=UINT32_C(0x0c170000)|wrong[i];
        compare(&changed,1578116u,UINT32_C(0x8ce00000),UINT32_C(0x8ce40000),UINT32_C(0x8cc00000),0x400000u);
    }
    for(unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);i++)
        compare(&h,lengths[i],UINT32_C(0x8ce00000),UINT32_C(0x8ce40000),UINT32_C(0x8cc00000),0x400000u);
    compare(&h,1578116u,UINT32_C(0x8ce00000),UINT32_C(0xace40000),UINT32_C(0x8cc00000),0x400000u);
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void actual_record(const char *path) {
    uint8_t wire[1024];FILE *file=fopen(path,"rb");CHECK(file);
    CHECK(fread(wire,1,sizeof(wire),file)==sizeof(wire));CHECK(fgetc(file)==EOF);CHECK(!fclose(file));
    CHECK(!memcmp(wire,"KUINVR01",8u));
    struct kui_vmu_native_heap h={0};h.dispatch_table=get32(wire+608u+12u*4u);h.sentinel=get32(wire+608u+13u*4u);
    uint32_t begin=get32(wire+692u),bytes=get32(wire+696u),image=get32(wire+24u);
    CHECK(kui_vmu_native_stage_allocation_safe(&h,image,UINT32_C(0x8ce00000),UINT32_C(0x8ce40000),begin,bytes));
    uint32_t payload=phys(begin)+bytes-0x30000u,header=payload-32u;
    /* Independent recorded initializer/allocate write footprint: metadata,
     * arena first8B, tail header+4 and the original low owner stack. The
     * conservative production ranges include each of those writes. */
    const uint32_t writes[]={h.sentinel-4u,h.sentinel,h.sentinel+4u,h.sentinel+0x20u,
        h.sentinel+0x24u,h.sentinel+0x28u,h.sentinel+0x2cu,begin,begin+4u,header+4u};
    for(unsigned i=0;i<sizeof(writes)/sizeof(writes[0]);i++) {
        uint32_t at=phys(writes[i]);CHECK(at+4u<=UINT32_C(0x0ce00000) || at>=UINT32_C(0x0ce40000));
    }
    CHECK(payload+0x20000u<=phys(begin)+bytes);
    if(phys(begin)+bytes==end_ram) {
        CHECK(!old_safe(&h,image,UINT32_C(0x0ce40000),begin,bytes));
        CHECK(header==UINT32_C(0x0cfcffe0) && payload==UINT32_C(0x0cfd0000));
    } else CHECK(old_safe(&h,image,UINT32_C(0x0ce40000),begin,bytes));
}
int main(int argc,char **argv) {
    direct_cases();boundaries();malformed();
    for(int i=1;i<argc;i++) actual_record(argv[i]);
    printf("Native temporary-stage placement: %u checks passed%s\n",checks,argc>1?" including actual cached heap geometry":"");
    return 0;
}
