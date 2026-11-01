/* SPDX-License-Identifier: GPL-3.0-only */
/* Reviewed no-allocator arena plans and their leases, against synthetic
 * images; the digests below are arbitrary test identities supplied by the
 * caller, as the stage and launcher supply the image's own SHA-256. An
 * optional real executable argument checks the reviewed plan itself; every
 * further argument must be refused. No retail bytes are stored here. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "kui/hash.h"
#define BASE UINT32_C(0x8c010000)
#define IMAGE 0x4000u
#define LOAD (BASE+0x200u)
#define SLOT (BASE+0x400u)
#define TARGET (BASE+0x1000u)
#define ARENA UINT32_C(0x8cdb9000)
#define PLAN(tag,begin,bytes) {IMAGE,{tag,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32}, \
    LOAD,LOAD+2u,SLOT,TARGET,begin,bytes}
/* The good plan, then arenas that overlap the image, are misaligned, too
 * small, or leave main RAM. */
#define KUI_VMU_NATIVE_HEAP_TEST_ARENA_PLAN \
    PLAN(0x51,ARENA,UINT32_C(0x47000)),PLAN(0x52,BASE+0x2000u,UINT32_C(0x47000)), \
    PLAN(0x53,ARENA+0x10u,UINT32_C(0x47000)),PLAN(0x54,ARENA,UINT32_C(0x47010)), \
    PLAN(0x55,ARENA,UINT32_C(0x60)),PLAN(0x56,UINT32_C(0x8cfe0000),UINT32_C(0x47000))
#include "../src/core/vmu_native_heap.c"
#include "../src/core/vmu_native_lifetime.c"

static unsigned checks,accepted;
#define CHECK(x) do {++checks;assert(x);} while(0)
static uint8_t image[IMAGE];
static void put16(size_t at,uint16_t v) {image[at]=(uint8_t)v;image[at+1u]=(uint8_t)(v>>8);}
static void put32(size_t at,uint32_t v) {put16(at,(uint16_t)v);put16(at+2u,(uint16_t)(v>>16));}
static void digest(uint8_t tag,uint8_t out[32]) {
    for(unsigned i=0;i<32u;i++) out[i]=(uint8_t)(i+1u);
    out[0]=tag;
}
static void fixture(void) {
    memset(image,0,sizeof(image));
    /* MOV.L @(127,PC),R2 reads SLOT; JSR @R2; NOP. */
    put16(LOAD-BASE,0xd27fu);put16(LOAD-BASE+2u,0x420bu);put16(LOAD-BASE+4u,0x0009u);
    put32(SLOT-BASE,TARGET);
}
static bool plan(uint8_t tag,struct kui_vmu_native_heap *owner,struct kui_vmu_native_heap_call *call,
    struct kui_vmu_native_heap_fixed_hook *hook) {
    uint8_t sha[32];digest(tag,sha);
    return kui_vmu_native_heap_arena_plan(image,IMAGE,sha,BASE,owner,call,hook);
}
/* The worker's own header words at the arena start, as the lifetime plan
 * reads them; one injected fault position. */
static uint32_t arena_words[8],fail_at=UINT32_MAX;
static bool reader(void *context,uint32_t address,uint32_t *value) {
    (void)context;
    if(address==fail_at) return false;
    uint32_t offset=(address&UINT32_C(0x1fffffff))-(ARENA&UINT32_C(0x1fffffff));
    if(offset>=32u || (offset&3u)) return false;
    *value=arena_words[offset/4u];return true;
}
static void synthetic_plans(void) {
    struct kui_vmu_native_heap owner;struct kui_vmu_native_heap_call call;
    struct kui_vmu_native_heap_fixed_hook hook;
    fixture();CHECK(plan(0x51,&owner,&call,&hook));
    CHECK(owner.version==KUI_VMU_NATIVE_HEAP_VERSION && owner.bytes==sizeof(owner) &&
        owner.family==KUI_VMU_NATIVE_HEAP_ARENA && !owner.layout && owner.image_bytes==IMAGE &&
        owner.load==BASE && owner.initializer_slot==SLOT && !owner.initializer && !owner.initializer_target &&
        !owner.allocate && !owner.release && !owner.sentinel && !owner.dispatch_table);
    CHECK(!metadata(&owner));
    CHECK(call.load==LOAD && call.call==LOAD+2u && call.return_pr==LOAD+6u &&
        call.entry_begin==ARENA && call.entry_bytes==UINT32_C(0x47000));
    CHECK(hook.slot==SLOT && hook.target==TARGET);
    /* Only the exact image identity selects a plan; a fixed (allocator)
     * plan's identity is not an arena plan, nor the converse. */
    CHECK(!plan(0x50,&owner,&call,&hook));CHECK(!plan(0x57,&owner,&call,&hook));
    uint8_t sha[32];digest(0x51,sha);sha[31]^=1u;
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE,sha,BASE,&owner,&call,&hook));
    digest(0x51,sha);
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE-2u,sha,BASE,&owner,&call,&hook));
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE,sha,BASE+2u,&owner,&call,&hook));
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE,sha,BASE+0x100u,&owner,&call,&hook));
    /* Each bad arena is refused for its own image. */
    for(uint8_t tag=0x52;tag<=0x56;tag++) CHECK(!plan(tag,&owner,&call,&hook));
    /* The literal must still name the planned target; one consumer and no
     * immutable address-of-slot word anywhere. */
    put32(SLOT-BASE,TARGET+2u);CHECK(!plan(0x51,&owner,&call,&hook));fixture();
    put16(0x100u,0xd3bfu);CHECK(!plan(0x51,&owner,&call,&hook));fixture();
    put32(0x800u,SLOT);CHECK(!plan(0x51,&owner,&call,&hook));fixture();
    put32(0x800u,(SLOT&UINT32_C(0x1fffffff))|UINT32_C(0xa0000000));CHECK(!plan(0x51,&owner,&call,&hook));fixture();
    put32(0x802u,SLOT);CHECK(plan(0x51,&owner,&call,&hook));fixture();
    /* Every 16-bit value at each of the three borrowed instructions. */
    for(unsigned position=0;position<3u;position++) {
        size_t at=LOAD-BASE+2u*position;uint16_t original=(uint16_t)(image[at]|image[at+1u]<<8);
        for(unsigned value=0;value<=UINT16_MAX;value++) {
            put16(at,(uint16_t)value);
            if(plan(0x51,&owner,&call,&hook)) {++accepted;CHECK(value==original);}
        }
        put16(at,original);
    }
    CHECK(accepted==3u);
    /* Outputs may not overlap the image or each other, nor be absent. */
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE,sha,BASE,(void *)image,&call,&hook));
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE,sha,BASE,&owner,(void *)(image+64u),&hook));
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE,sha,BASE,&owner,&call,(void *)&call));
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE,sha,BASE,&owner,(void *)&owner,&hook));
    CHECK(!kui_vmu_native_heap_arena_plan(NULL,IMAGE,sha,BASE,&owner,&call,&hook));
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE,NULL,BASE,&owner,&call,&hook));
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE,sha,BASE,NULL,&call,&hook));
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE,sha,BASE,&owner,NULL,&hook));
    CHECK(!kui_vmu_native_heap_arena_plan(image,IMAGE,sha,BASE,&owner,&call,NULL));
    /* An allocator plan never accepts the arena identity. */
    struct kui_vmu_native_heap tail=owner;tail.family=KUI_VMU_NATIVE_HEAP_TAIL32;
    CHECK(!kui_vmu_native_heap_fixed_plan(image,IMAGE,sha,&tail,&call,&hook));
}
static void leases(void) {
    struct kui_vmu_native_heap owner;struct kui_vmu_native_heap_call call;
    struct kui_vmu_native_heap_fixed_hook hook;struct kui_vmu_native_heap_lease lease,bad;
    fixture();CHECK(plan(0x51,&owner,&call,&hook));
    CHECK(kui_vmu_native_heap_arena_lease(&owner,call.entry_begin,call.entry_bytes,UINT32_C(0x47000),&lease));
    CHECK(lease.phase==2u && lease.initializations==1u && lease.begin==ARENA && lease.payload==ARENA &&
        lease.heap_bytes==UINT32_C(0x47000) && lease.lease_bytes==UINT32_C(0x47000) &&
        !lease.header && !lease.units && !lease.remaining);
    CHECK(kui_vmu_native_heap_lease_valid(&lease,NULL,NULL,1u));
    CHECK(!kui_vmu_native_heap_lease_valid(&lease,NULL,NULL,2u));
    CHECK(!kui_vmu_native_heap_lease_valid(&lease,NULL,NULL,0u));
    /* Allocator-only proofs never accept an arena lease. */
    CHECK(!kui_vmu_native_heap_lease_identity_valid(&lease,reader,NULL,1u));
    uint32_t released=0u;
    CHECK(!kui_vmu_native_heap_lease_valid_with_release(&lease,reader,NULL,1u,0u,&released));
    CHECK(!kui_vmu_native_lifetime_release_safe(&lease,reader,NULL,1u,0u));
    CHECK(!kui_vmu_native_heap_arena_lease(&owner,call.entry_begin,call.entry_bytes,UINT32_C(0x46000),&lease));
    CHECK(!kui_vmu_native_heap_arena_lease(&owner,ARENA+0x20u,UINT32_C(0x47000),UINT32_C(0x47000),&bad) ||
          kui_vmu_native_heap_lease_valid(&bad,NULL,NULL,1u));
    CHECK(!kui_vmu_native_heap_arena_lease(&owner,ARENA+0x10u,UINT32_C(0x47000),UINT32_C(0x47000),&bad));
    CHECK(!kui_vmu_native_heap_arena_lease(&owner,BASE+0x2000u,UINT32_C(0x47000),UINT32_C(0x47000),&bad));
    CHECK(!kui_vmu_native_heap_arena_lease(&owner,UINT32_C(0x8cfe0000),UINT32_C(0x47000),UINT32_C(0x47000),&bad));
    CHECK(!kui_vmu_native_heap_arena_lease(NULL,ARENA,UINT32_C(0x47000),UINT32_C(0x47000),&bad));
    CHECK(!kui_vmu_native_heap_arena_lease(&owner,ARENA,UINT32_C(0x47000),UINT32_C(0x47000),
                                           (void *)&owner));
    /* Every scalar of the lease and its owner is exact. */
    for(size_t word=0;word<sizeof(lease)/4u;word++) for(unsigned bit=0;bit<32u;bit+=7u) {
        bad=lease;((uint32_t *)(void *)&bad)[word]^=UINT32_C(1)<<bit;
        bool valid=kui_vmu_native_heap_lease_valid(&bad,NULL,NULL,1u);
        /* The image/slot words may move to other valid geometry; nothing
         * else may change. */
        size_t owner_first=offsetof(struct kui_vmu_native_heap_lease,owner)/4u;
        size_t image_word=owner_first+offsetof(struct kui_vmu_native_heap,image_bytes)/4u;
        size_t slot_word=owner_first+offsetof(struct kui_vmu_native_heap,initializer_slot)/4u;
        size_t load_word=owner_first+offsetof(struct kui_vmu_native_heap,load)/4u;
        if(valid) CHECK(word==image_word || word==slot_word || word==load_word);
    }
    bad=lease;bad.owner.family=KUI_VMU_NATIVE_HEAP_TAIL32;CHECK(!kui_vmu_native_heap_lease_valid(&bad,reader,NULL,1u));
    bad=lease;bad.owner.initializer_slot=BASE+IMAGE;CHECK(!kui_vmu_native_heap_lease_valid(&bad,NULL,NULL,1u));
    bad=lease;bad.owner.release=TARGET;CHECK(!kui_vmu_native_heap_lease_valid(&bad,NULL,NULL,1u));
    bad=lease;bad.owner.image_bytes=UINT32_C(0xd00000);CHECK(!kui_vmu_native_heap_lease_valid(&bad,NULL,NULL,1u));
    bad=lease;bad.header=ARENA-32u;CHECK(!kui_vmu_native_heap_lease_valid(&bad,NULL,NULL,1u));
    bad=lease;bad.payload=ARENA+32u;CHECK(!kui_vmu_native_heap_lease_valid(&bad,NULL,NULL,1u));
}
static void lifetimes(void) {
    struct kui_vmu_native_heap owner;struct kui_vmu_native_heap_call call;
    struct kui_vmu_native_heap_fixed_hook hook;struct kui_vmu_native_heap_lease lease;
    fixture();CHECK(plan(0x51,&owner,&call,&hook));
    CHECK(kui_vmu_native_heap_arena_lease(&owner,ARENA,UINT32_C(0x47000),UINT32_C(0x47000),&lease));
    for(unsigned i=0;i<8u;i++) arena_words[i]=UINT32_C(0x4b4e5031)+i*0x10101u;
    static struct kui_vmu_native_lifetime_plan out;
    const struct kui_vmu_native_lifetime_range outside[]={{BASE,IMAGE},{ARENA-0x100u,0x100u},
        {ARENA+UINT32_C(0x47000),0x40u},{UINT32_C(0xac0c0000),0x1100u}};
    CHECK(kui_vmu_native_lifetime_prepare(&out,&lease,reader,NULL,1u,outside,4u));
    CHECK(out.self==(uintptr_t)&out && out.ready==KUI_VMU_NATIVE_LIFETIME_READY && !out.count &&
        out.begin==(ARENA&UINT32_C(0x1fffffff)) && out.end==out.begin+UINT32_C(0x47000) &&
        out.lease_begin==out.begin && out.lease_end==out.end && !out.sentinel && !out.cursor);
    for(unsigned i=0;i<8u;i++) CHECK(out.identity[i].address==ARENA+4u*i && out.identity[i].value==arena_words[i]);
    CHECK(kui_vmu_native_lifetime_identity_prepared_inline(&out,reader,NULL,1u));
    arena_words[5]^=1u;CHECK(!kui_vmu_native_lifetime_identity_prepared_inline(&out,reader,NULL,1u));
    arena_words[5]^=1u;
    /* No protected SDK span may lie inside the arena, at either edge. */
    const struct kui_vmu_native_lifetime_range inside[]={{ARENA+0x100u,4u},{ARENA-4u,8u},
        {ARENA+UINT32_C(0x47000)-4u,8u},{(ARENA&UINT32_C(0x1fffffff))|UINT32_C(0xa0000000),4u}};
    for(unsigned i=0;i<4u;i++) CHECK(!kui_vmu_native_lifetime_prepare(&out,&lease,reader,NULL,1u,&inside[i],1u));
    /* Each identity read must succeed; a missing reader refuses. */
    for(unsigned i=0;i<8u;i++) {
        fail_at=ARENA+4u*i;CHECK(!kui_vmu_native_lifetime_prepare(&out,&lease,reader,NULL,1u,NULL,0u));
    }
    fail_at=UINT32_MAX;
    CHECK(!kui_vmu_native_lifetime_prepare(&out,&lease,NULL,NULL,1u,NULL,0u));
    CHECK(!kui_vmu_native_lifetime_prepare(&out,&lease,reader,NULL,2u,NULL,0u));
    CHECK(kui_vmu_native_lifetime_prepare(&out,&lease,reader,NULL,1u,NULL,0u));
}
static uint8_t *load_file(const char *path,size_t *bytes) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
    uint8_t *data=malloc((size_t)n);CHECK(data);CHECK(fread(data,1,(size_t)n,f)==(size_t)n);fclose(f);
    *bytes=(size_t)n;return data;
}
static bool real_plan(const uint8_t *data,size_t n,struct kui_vmu_native_heap *owner,
    struct kui_vmu_native_heap_call *call,struct kui_vmu_native_heap_fixed_hook *hook) {
    struct kui_sha256 s;uint8_t sha[32];
    kui_sha256_init(&s);kui_sha256_update(&s,data,n);kui_sha256_digest(&s,sha);
    return kui_vmu_native_heap_arena_plan(data,n,sha,BASE,owner,call,hook);
}
int main(int argc,char **argv) {
    synthetic_plans();leases();lifetimes();
    unsigned refused=0u;
    if(argc>=2) {
        /* The reviewed House of the Dead 2 plan, on the owner's executable. */
        size_t n;uint8_t *data=load_file(argv[1],&n);
        struct kui_vmu_native_heap owner;struct kui_vmu_native_heap_call call;
        struct kui_vmu_native_heap_fixed_hook hook;
        CHECK(real_plan(data,n,&owner,&call,&hook));
        CHECK(owner.family==KUI_VMU_NATIVE_HEAP_ARENA && owner.image_bytes==n && owner.load==BASE &&
            owner.initializer_slot==UINT32_C(0x8c01b184));
        CHECK(call.load==UINT32_C(0x8c01b112) && call.call==UINT32_C(0x8c01b114) &&
            call.return_pr==UINT32_C(0x8c01b118) && call.entry_begin==UINT32_C(0x8cdb9000) &&
            call.entry_bytes==UINT32_C(0x47000));
        CHECK(hook.slot==UINT32_C(0x8c01b184) && hook.target==UINT32_C(0x0c0ae9c4));
        struct kui_vmu_native_heap_lease lease;
        CHECK(kui_vmu_native_heap_arena_lease(&owner,call.entry_begin,call.entry_bytes,
                                              UINT32_C(0x47000),&lease));
        CHECK(kui_vmu_native_heap_lease_valid(&lease,NULL,NULL,1u));
        /* The image has no SDK allocator for the ordinary paths to find. */
        struct kui_vmu_native_heap heap;
        CHECK(kui_vmu_native_heap_discover(data,n,BASE,&heap)==KUI_VMU_NATIVE_HEAP_NONE);
        /* Any changed byte of the borrowed call or literal changes the
         * identity; with the digest recomputed it is still refused. */
        const size_t sites[]={0xb112u,0xb113u,0xb114u,0xb115u,0xb116u,0xb117u,0xb184u,0xb186u};
        for(unsigned i=0;i<sizeof(sites)/sizeof(sites[0]);i++) {
            data[sites[i]]^=1u;CHECK(!real_plan(data,n,&owner,&call,&hook));data[sites[i]]^=1u;
        }
        CHECK(real_plan(data,n,&owner,&call,&hook));free(data);
        /* No other executable selects the arena plan. */
        for(int arg=2;arg<argc;arg++) {
            data=load_file(argv[arg],&n);CHECK(!real_plan(data,n,&owner,&call,&hook));free(data);++refused;
        }
    }
    printf("Native no-allocator arena: %u checks; 196608 instruction mutations, only the reviewed call admitted;"
        " %s; %u other executables refused\n",checks,argc>=2?"reviewed House of the Dead 2 plan verified":
        "no real executable given",refused);
    return 0;
}
