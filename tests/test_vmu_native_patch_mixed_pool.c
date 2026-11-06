/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently authored SH literal pools. Owner originals are optional,
 * read-only arguments; mutations exist only in private heap copies. */
#include "kui/vmu_native_patch.h"
#include "kui/vmu_native_layout.h"
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_exit.h"
#include "kui/vmu_native_resolution_cache.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { IMAGE_BYTES=8192, POOL=0x300, PATCH=0x100 };
static const uint32_t load=UINT32_C(0x8c010000);
static uint8_t image[IMAGE_BYTES];
static unsigned checks;
#define CHECK(test) do {++checks;if(!(test)) {fprintf(stderr,"mixed pool check failed at line %u\n",__LINE__);exit(1);}} while(0)
static void put16(uint8_t *p,uint16_t value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void put32(uint8_t *p,uint32_t value) {for(unsigned i=0u;i<4u;i++)p[i]=(uint8_t)(value>>(8u*i));}
static uint16_t get16(const uint8_t *p) {return (uint16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8);}
static uint16_t branch(unsigned source,unsigned target,uint16_t opcode) {
    int displacement=((int)target-(int)source-4)/2;
    CHECK(!(source&1u) && !(target&1u));
    CHECK(displacement>=-2048 && displacement<=2047);
    return (uint16_t)(opcode|((unsigned)displacement&4095u));
}
static void movl(unsigned at,unsigned target) {
    unsigned pc=(at+4u)&~3u;CHECK(!(target&3u) && target>=pc && (target-pc)/4u<=255u);
    put16(image+at,(uint16_t)(0xd300u|(target-pc)/4u));
}
static void pool(unsigned start,unsigned words,bool padded,uint16_t delay) {
    CHECK(!(start&3u) && words>=3u && start+4u*words<sizeof(image));
    unsigned cutoff=start-(padded?6u:4u);
    put16(image+cutoff,0x000bu);put16(image+cutoff+2u,delay);
    if(padded)put16(image+start-2u,0u);
    for(unsigned i=0u;i<words;i++) {
        put32(image+start+4u*i,UINT32_C(0x8c020100)+4u*i);
        movl(start-0x80u+2u*i,start+4u*i);
    }
    put32(image+start+4u,UINT32_C(0x06000001));
    put32(image+start+8u,UINT32_C(0x8c010000)|branch(start+8u,PATCH+4u,0xb000u));
}
static void fixture(bool padded,uint16_t delay) {
    memset(image,0,sizeof(image));pool(POOL,4u,padded,delay);
}
static bool guarded(void) {
    return kui_vmu_native_patch_no_interior(image,sizeof(image),load,load+PATCH,12u);
}
/* A separately callable one-way tail veneer precedes this pure pointer pool.
 * Two independent synthetic IRQ restorers share its 16-bit mask. */
static void irq_restore_into(uint8_t *bytes,unsigned at,unsigned mask) {
    static const uint16_t words[]={0x0202u,0x60c3u,0u,0xc90fu,0x4008u,
        0x2239u,0x4008u,0x202bu,0x400eu};
    for(unsigned i=0u;i<9u;i++)put16(bytes+at+2u*i,words[i]);
    CHECK(mask>=at+8u && !(mask&1u) && (mask-at-8u)/2u<=255u);
    put16(bytes+at+4u,(uint16_t)(0x9300u|((mask-at-8u)/2u)));
}
static void irq_restore(unsigned at,unsigned mask) {irq_restore_into(image,at,mask);}
static void veneer_fixture(void) {
    fixture(false,0x0009u);
    for(unsigned i=0u;i<5u;i++) {
        put32(image+0x600u+4u*i,UINT32_C(0x8c020100)+4u*i);
        if(i<4u)movl(0x580u+2u*i,0x600u+4u*i);
    }
    put32(image+0x608u,UINT32_C(0x8c010000)|branch(0x608u,POOL,0xa000u));
    put32(image+0x610u,load+0x740u);
    put16(image+0x5f4u,0x000bu);put16(image+0x5f6u,0x6ef6u);
    movl(0x5f8u,0x610u);put16(image+0x5fau,0x432bu);put16(image+0x5fcu,0x0009u);
    put16(image+0x5feu,0xff0fu);irq_restore(0x520u,0x5feu);irq_restore(0x540u,0x5feu);
}
static void veneer_dependencies(void) {
    veneer_fixture();CHECK(guarded());
    for(unsigned source=0x520u;source<=0x540u;source+=0x20u)
        for(unsigned i=0u;i<9u;i++)if(i!=2u) {
            veneer_fixture();put16(image+source+2u*i,(uint16_t)(get16(image+source+2u*i)^1u));
            CHECK(!guarded());
        }
    veneer_fixture();put16(image+0x5feu,0xff1fu);CHECK(!guarded());
    veneer_fixture();put16(image+0x5fau,0x422bu);CHECK(!guarded());
    veneer_fixture();put16(image+0x5fcu,0xe001u);CHECK(!guarded());
    veneer_fixture();put16(image+0x5f4u,0x0009u);CHECK(!guarded());
    veneer_fixture();put16(image+0x524u,0x0009u);CHECK(!guarded()); /* Exactly one reader. */
    veneer_fixture();irq_restore(0x4e0u,0x5feu);CHECK(!guarded()); /* Three complete readers. */
    veneer_fixture();put16(image+0x524u,(uint16_t)(get16(image+0x524u)+1u));CHECK(!guarded());
    veneer_fixture();movl(0x5f8u,0x60cu);CHECK(!guarded()); /* Veneer must use the final word. */
    veneer_fixture();put32(image+0x610u,load+0x741u);CHECK(!guarded());
    veneer_fixture();put32(image+0x610u,load+sizeof(image));CHECK(!guarded());
    veneer_fixture();put32(image+0x40u,load+0x5feu);CHECK(!guarded());
    veneer_fixture();put16(image+0x500u,(uint16_t)(0x9000u|(0x5feu-0x504u)/2u));CHECK(!guarded());
    veneer_fixture();put16(image+0x500u,(uint16_t)(0x9000u|(0x600u-0x504u)/2u));CHECK(!guarded());
    veneer_fixture();put16(image+0x700u,branch(0x700u,0x5feu,0xa000u));CHECK(!guarded());
    veneer_fixture();put16(image+0x700u,branch(0x700u,0x5fcu,0xa000u));CHECK(!guarded());
}
static void synthetics(void) {
    const uint16_t delays[]={0x0009u,0x60f6u,0x6ef6u};
    for(unsigned padded=0u;padded<2u;padded++)for(unsigned d=0u;d<3u;d++) {
        fixture(padded!=0u,delays[d]);CHECK(guarded());
        for(unsigned i=0u;i<4u;i++) {
            fixture(padded!=0u,delays[d]);put16(image+POOL-0x80u+2u*i,0x0009u);CHECK(!guarded());
        }
        fixture(padded!=0u,delays[d]);put16(image+POOL-(padded?6u:4u),0x0009u);CHECK(!guarded());
        fixture(padded!=0u,delays[d]);put16(image+POOL-(padded?4u:2u),0xe001u);CHECK(!guarded());
    }
    fixture(true,0x0009u);put16(image+POOL-2u,0xffffu);CHECK(guarded());
    fixture(true,0x0009u);put16(image+POOL-2u,0x0009u);CHECK(!guarded());
    fixture(false,0x6ff6u);CHECK(!guarded()); /* POP SP is not a safe delay. */
    for(unsigned target=POOL-2u;target<POOL+16u;target++) {
        fixture(false,0x0009u);put32(image+0x20u,load+target);CHECK(!guarded());
    }
    for(unsigned target=POOL-2u;target<POOL+16u;target+=2u) {
        fixture(false,0x0009u);
        put16(image+0x3c0u,branch(0x3c0u,target,0xa000u));CHECK(!guarded());
        fixture(false,0x0009u);unsigned pc=0x260u;
        put16(image+pc,(uint16_t)(0x9000u|((target-pc-4u)/2u)));CHECK(!guarded());
    }
    for(unsigned target=POOL;target<POOL+16u;target+=4u) {
        fixture(false,0x0009u);unsigned pc=0x240u;
        put16(image+pc,(uint16_t)(0xc700u|((target-((pc+4u)&~3u))/4u)));CHECK(!guarded());
        fixture(false,0x0009u);movl(POOL-4u,target);CHECK(!guarded()); /* RTS itself may not be a reference. */
    }
    fixture(false,0x0009u);put32(image+POOL+12u,load+PATCH+2u);CHECK(!guarded());
    fixture(false,0x0009u);put16(image+0x3c0u,branch(0x3c0u,PATCH+2u,0xb000u));CHECK(!guarded());
    fixture(false,0x0009u);
    put32(image+POOL+12u,UINT32_C(0x06000000)|branch(POOL+12u,PATCH+6u,0xb000u));
    put32(image+POOL+16u,UINT32_C(0x8c020100));movl(0x288u,POOL+16u);
    CHECK(!guarded()); /* An apparent branch cannot borrow a second scalar. */
    memset(image,0,sizeof(image));pool(POOL,32u,false,0x0009u);CHECK(guarded());
    memset(image,0,sizeof(image));pool(POOL,33u,false,0x0009u);CHECK(!guarded());
    fixture(false,0x0009u);
    const struct kui_vmu_native_patch_window overlap[]={{load+PATCH,12u},{load+POOL+8u,4u}};
    CHECK(!kui_vmu_native_patch_no_interiors(image,sizeof(image),load,overlap,2u));
    fixture(false,0x0009u);pool(0x600u,4u,false,0x0009u);
    put32(image+POOL+12u,UINT32_C(0x8c010000)|branch(POOL+12u,0x600u,0xa000u));
    put32(image+0x600u+8u,UINT32_C(0x8c010000)|branch(0x608u,POOL,0xa000u));CHECK(!guarded());
    memset(image,0,sizeof(image));
    for(unsigned i=0u;i<5u;i++) {
        unsigned start=POOL+0x100u*i;pool(start,4u,false,0x0009u);
        if(i)put32(image+start+8u,UINT32_C(0x8c010000)|branch(start+8u,start-0x100u,0xa000u));
    }
    CHECK(!guarded()); /* Five live dependencies exceed the private stack bound. */
}
static uint8_t *read_original(const char *path,size_t *bytes) {
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long size=ftell(file);
    CHECK(size>0 && size<0xc00000 && !fseek(file,0,SEEK_SET));*bytes=(size_t)size;
    uint8_t *data=malloc(*bytes);CHECK(data);CHECK(fread(data,1u,*bytes,file)==*bytes);CHECK(!fclose(file));return data;
}
static void source_digest(const uint8_t *data,size_t bytes,char hex[65]) {
    static const char digits[]="0123456789abcdef";struct kui_sha256 hash;uint8_t digest[32];
    kui_sha256_init(&hash);kui_sha256_update(&hash,data,bytes);kui_sha256_digest(&hash,digest);
    for(unsigned i=0u;i<32u;i++) {hex[2u*i]=digits[digest[i]>>4];hex[2u*i+1u]=digits[digest[i]&15u];}hex[64]=0;
}
static void original(const char *path,bool engine) {
    size_t bytes;uint8_t *data=read_original(path,&bytes);char digest[65];source_digest(data,bytes,digest);
    CHECK(bytes==(engine?1848288u:212324u));
    CHECK(!strcmp(digest,engine?"04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61":
        "3a311b189a4870ccbefc4fcd306e2d38e5ae810ae5edd225af6b9aa6d5df5f10"));
    struct kui_vmu_native_discovery driver;struct kui_vmu_native_layout layout;
    struct kui_vmu_native_heap heap;struct kui_vmu_native_heap_call call;
    struct kui_vmu_native_exit exits;struct kui_vmu_native_resolution resolution;
    uint8_t wire[1024];
    CHECK(kui_vmu_native_discover_layout(data,bytes,load,&driver,&layout));
    CHECK(kui_vmu_native_heap_discover(data,bytes,load,&heap)==KUI_VMU_NATIVE_HEAP_FOUND);
    CHECK(kui_vmu_native_heap_call_discover(data,bytes,&heap,&call));
    const struct kui_vmu_native_patch_window extra[]={{driver.submit,12u},{heap.initializer,12u},{heap.release,20u}};
    CHECK(kui_vmu_native_exit_discover_guarded(data,bytes,load,extra,3u,&exits));
    CHECK(exits.family==KUI_VMU_NATIVE_EXIT_FIRMWARE_CHECK && exits.count==3u);
    CHECK(kui_vmu_native_resolution_cache_build(data,bytes,load,wire,sizeof(wire),&resolution));
    uint8_t saved[4];memcpy(saved,data,4u);
    for(unsigned entry=0u;entry<exits.count;entry++) {
        put32(data,exits.entries[entry]+2u);
        CHECK(!kui_vmu_native_exit_discover(data,bytes,load,&exits));memcpy(data,saved,4u);
        CHECK(kui_vmu_native_exit_discover(data,bytes,load,&exits));
    }
    if(engine) {
        const unsigned root=0x19bdf6u,reference=0x19bdd0u;
        uint16_t previous=get16(data+root);put16(data+root,0x0009u);
        CHECK(!kui_vmu_native_exit_discover(data,bytes,load,&exits));put16(data+root,previous);
        previous=get16(data+reference);put16(data+reference,0x0009u);
        CHECK(!kui_vmu_native_exit_discover(data,bytes,load,&exits));put16(data+reference,previous);
        CHECK(kui_vmu_native_exit_discover(data,bytes,load,&exits));
        const unsigned dependency_words[]={0x19c034u,0x19c036u,0x19c038u,0x19c03au,0x19c03cu,0x19c03eu};
        for(unsigned i=0u;i<sizeof(dependency_words)/sizeof(dependency_words[0]);i++) {
            unsigned at=dependency_words[i];previous=get16(data+at);put16(data+at,(uint16_t)(previous^1u));
            CHECK(!kui_vmu_native_exit_discover(data,bytes,load,&exits));put16(data+at,previous);
        }
        const unsigned restorers[]={0x19bfecu,0x19c004u};
        for(unsigned s=0u;s<2u;s++)for(unsigned i=0u;i<9u;i++) {
            unsigned at=restorers[s]+2u*i;previous=get16(data+at);put16(data+at,(uint16_t)(previous^1u));
            CHECK(!kui_vmu_native_exit_discover(data,bytes,load,&exits));put16(data+at,previous);
        }
        previous=get16(data+0x19bff0u);put16(data+0x19bff0u,0x0009u);
        CHECK(!kui_vmu_native_exit_discover(data,bytes,load,&exits));put16(data+0x19bff0u,previous);
        uint8_t saved_third[18];memcpy(saved_third,data+0x19bf00u,sizeof(saved_third));
        irq_restore_into(data,0x19bf00u,0x19c03eu);
        CHECK(!kui_vmu_native_exit_discover(data,bytes,load,&exits));
        memcpy(data+0x19bf00u,saved_third,sizeof(saved_third));
        CHECK(kui_vmu_native_exit_discover(data,bytes,load,&exits));
    }
    char again[65];source_digest(data,bytes,again);CHECK(!strcmp(again,digest));
    printf("PASS read-only %s: driver/layout/heap/call/exits/cache, root/reference mutations, restored source SHA256=%s\n",
        engine?"MDE_US engine":"Smash Pack menu",digest);free(data);
}
int main(int argc,char **argv) {
    CHECK(argc==1 || argc==3);synthetics();veneer_dependencies();
    if(argc==3) {original(argv[1],true);original(argv[2],false);}
    printf("PASS mixed-pool guard: %u synthetic and original checks\n",checks);return 0;
}
