/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_patch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Exercise the public guard, never the private classifier. Every fixture has
 * a real apparent BRA from a RAM-pointer word into the patch interior, so an
 * uncalled or permanently refusing fallback cannot pass the positive cases.
 * The optional owner-supplied original is never written. */
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"literal-pool guard failed at %u\n",__LINE__);exit(1);}} while(0)
enum { BYTES=4096u, POOL=1024u, HOOK=1536u, REFS=512u, ENTRY=896u };
static const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
static void half(unsigned char *p,uint16_t v) {p[0]=(unsigned char)v;p[1]=(unsigned char)(v>>8);}
static uint16_t h16(const unsigned char *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static void word(unsigned char *p,uint32_t v) {for(unsigned i=0;i<4u;i++) p[i]=(unsigned char)(v>>(8u*i));}
static uint32_t h32(const unsigned char *p) {return (uint32_t)h16(p)|(uint32_t)h16(p+2u)<<16;}
static uint16_t branch(uint16_t shape,uint32_t from,uint32_t to) {
    int32_t displacement=((int32_t)to-(int32_t)from-4)/2;
    return (uint16_t)(shape|((uint32_t)displacement&((shape&0xf000u)==0xa000u ||
        (shape&0xf000u)==0xb000u?4095u:255u)));
}
static uint16_t mov_long(uint32_t load,uint32_t from,uint32_t to,unsigned reg) {
    uint32_t base=(load+from+4u)&~3u,target=load+to;
    CHECK(target>=base && target-base<=1020u && !((target-base)&3u));
    return (uint16_t)(0xd000u|(reg<<8)|((target-base)/4u));
}
static bool admitted(const unsigned char *data,size_t n,uint32_t load,uint32_t hook) {
    const struct kui_vmu_native_patch_window windows[]={
        {((load&UINT32_C(0x1fffffff))+hook)|UINT32_C(0xa0000000),12u}};
    bool batch=kui_vmu_native_patch_no_interiors(data,n,load,windows,1u);
    CHECK(batch==kui_vmu_native_patch_no_interior(data,n,load,windows[0].begin,12u));
    return batch;
}
static void fixture(unsigned char data[BYTES],uint32_t load,uint32_t alias,unsigned count,uint16_t delay) {
    for(unsigned i=0;i<BYTES;i+=2u) half(data+i,9u);
    half(data+POOL-4u,0x000bu);half(data+POOL-2u,delay);
    for(unsigned i=0;i<count;i++) {
        uint32_t value=(UINT32_C(0x0c020000)+4u*i)|alias;
        if(!i) value=(UINT32_C(0x0c010000)|alias)|branch(0xa000u,POOL,HOOK+2u);
        word(data+POOL+4u*i,value);
        half(data+REFS+2u*i,mov_long(load,REFS+2u*i,POOL+4u*i,i%15u));
    }
}
static void synthetic(void) {
    unsigned char base[BYTES],data[BYTES];
    const uint16_t branches[]={0xa000u,0xb000u,0x8900u,0x8b00u,0x8d00u,0x8f00u};
    for(unsigned load_alias=0;load_alias<3u;load_alias++) for(unsigned pool_alias=0;pool_alias<3u;pool_alias++) {
        uint32_t load=UINT32_C(0x0c010000)|aliases[load_alias],alias=aliases[pool_alias];
        for(unsigned boundary=0;boundary<2u;boundary++) {
            unsigned count=boundary?32u:2u;
            fixture(base,load,alias,count,9u);CHECK(admitted(base,BYTES,load,HOOK));
            const struct kui_vmu_native_patch_window overlaps[]={
                {load+HOOK,12u},{load+POOL,4u}};
            CHECK(!kui_vmu_native_patch_no_interiors(base,BYTES,load,overlaps,2u));
            for(unsigned reg=0;reg<16u;reg++) {
                memcpy(data,base,sizeof(data));half(data+POOL-2u,(uint16_t)(0x60f6u|(reg<<8)));
                CHECK(admitted(data,BYTES,load,HOOK)==(reg!=15u));
            }
            /* Every word must retain a positive full-word reference. */
            for(unsigned i=0;i<count;i++) {
                memcpy(data,base,sizeof(data));half(data+REFS+2u*i,9u);
                CHECK(!admitted(data,BYTES,load,HOOK));
            }
            /* All six encoded direct branches into every halfword of the
             * delay/pool span reject, including a source in the pool. */
            for(unsigned shape=0;shape<6u;shape++) for(unsigned target=POOL-2u;target<POOL+count*4u;target+=2u) {
                memcpy(data,base,sizeof(data));half(data+ENTRY,branch(branches[shape],ENTRY,target));
                CHECK(!admitted(data,BYTES,load,HOOK));
            }
            for(unsigned target=POOL-2u;target<POOL+count*4u;target+=2u) for(unsigned incoming=0;incoming<3u;incoming++) {
                memcpy(data,base,sizeof(data));word(data+ENTRY,((load&UINT32_C(0x1fffffff))+target)|aliases[incoming]);
                CHECK(!admitted(data,BYTES,load,HOOK));
            }
            memcpy(data,base,sizeof(data));half(data+POOL-4u,9u);CHECK(!admitted(data,BYTES,load,HOOK));
            memcpy(data,base,sizeof(data));half(data+POOL-2u,0x4f26u);CHECK(!admitted(data,BYTES,load,HOOK));
            /* Maximality: neither a referenced prefix of a longer run nor a
             * suffix following a fabricated delimiter is enough. */
            memcpy(data,base,sizeof(data));word(data+POOL+count*4u,UINT32_C(0x8c020000));
            CHECK(!admitted(data,BYTES,load,HOOK));
            memcpy(data,base,sizeof(data));word(data+POOL-4u,UINT32_C(0x8c020000));
            CHECK(!admitted(data,BYTES,load,HOOK));
            for(unsigned target=POOL-2u;target<POOL+count*4u;target+=2u) {
                memcpy(data,base,sizeof(data));half(data+ENTRY,(uint16_t)(0x9000u|((target-ENTRY-4u)/2u)));
                CHECK(!admitted(data,BYTES,load,HOOK));
                if(!(target&3u)) {
                    memcpy(data,base,sizeof(data));half(data+ENTRY,(uint16_t)(0xc700u|((target-((ENTRY+4u)&~3u))/4u)));
                    CHECK(!admitted(data,BYTES,load,HOOK));
                }
            }
            /* The ordinary aligned pointer-to-hook rejection still runs in
             * accepted data, and still normalizes every raw alias. */
            for(unsigned incoming=0;incoming<3u;incoming++) {
                memcpy(data,base,sizeof(data));word(data+POOL+4u,((load&UINT32_C(0x1fffffff))+HOOK+2u)|aliases[incoming]);
                CHECK(!admitted(data,BYTES,load,HOOK));
            }
            memcpy(data,base,sizeof(data));
            word(data+POOL+4u,(UINT32_C(0x0c010000)|alias)|branch(0xa000u,POOL+4u,POOL));
            CHECK(!admitted(data,BYTES,load,HOOK));
            if(count==32u) {
                memcpy(data,base,sizeof(data));word(data+POOL+4u,UINT32_C(0x0c01d000)|alias);
                CHECK(!admitted(data,BYTES,load,HOOK)); /* MOV.L inside pool */
            }
        }
        fixture(data,load,alias,1u,9u);CHECK(!admitted(data,BYTES,load,HOOK));
        fixture(data,load,alias,33u,9u);CHECK(!admitted(data,BYTES,load,HOOK));
        fixture(data,load,alias,2u,9u);
        word(data+POOL,(UINT32_C(0x0c010000)|alias)|branch(0xa000u,POOL,POOL-10u));
        CHECK(!admitted(data,POOL+6u,load,POOL-12u));
    }
}
static void original(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long length=ftell(f);CHECK(length==686692);rewind(f);
    size_t bytes=(size_t)length;unsigned char *source=malloc(bytes),*data=malloc(bytes);CHECK(source && data);
    CHECK(fread(source,1,bytes,f)==bytes);CHECK(!fclose(f));
    const uint32_t load=UINT32_C(0x8c010000),pool=UINT32_C(0x524cc),end=UINT32_C(0x52528);
    const struct kui_vmu_native_patch_window windows[]={{UINT32_C(0x8c061654),8u},{UINT32_C(0x8c06165c),12u}};
    CHECK(h16(source+pool-4u)==0x000bu && h16(source+pool-2u)==0x6ef6u);
    CHECK(h32(source+0x524d0u)==UINT32_C(0x8c39a8c8));
    CHECK(h32(source+0x524e4u)==UINT32_C(0x8c39a8b8));
    for(unsigned a=0;a<3u;a++) {
        uint32_t alias_load=(load&UINT32_C(0x1fffffff))|aliases[a];
        CHECK(kui_vmu_native_patch_no_interiors(source,bytes,alias_load,windows,2u));
    }
    for(uint32_t word_at=pool;word_at<end;word_at+=4u) {
        memcpy(data,source,bytes);unsigned references=0;
        for(uint32_t at=0;at+2u<=pool-4u;at+=2u) {
            uint16_t op=h16(source+at);
            if((op&0xf000u)==0xd000u && (((load+at+4u)&~3u)+4u*(op&255u))==load+word_at) {
                half(data+at,9u);++references;
            }
        }
        CHECK(references>0u);CHECK(!kui_vmu_native_patch_no_interiors(data,bytes,load,windows,2u));
    }
    memcpy(data,source,bytes);half(data+pool-2u,9u);
    CHECK(kui_vmu_native_patch_no_interiors(data,bytes,load,windows,2u)); /* valid alternative delay */
    memcpy(data,source,bytes);half(data+pool-4u,9u);
    CHECK(!kui_vmu_native_patch_no_interiors(data,bytes,load,windows,2u));
    for(unsigned a=0;a<3u;a++) {
        memcpy(data,source,bytes);word(data+pool,((windows[0].begin+2u)&UINT32_C(0x1fffffff))|aliases[a]);
        CHECK(!kui_vmu_native_patch_no_interiors(data,bytes,load,windows,2u));
        memcpy(data,source,bytes);word(data,((load+pool-2u)&UINT32_C(0x1fffffff))|aliases[a]);
        CHECK(!kui_vmu_native_patch_no_interiors(data,bytes,load,windows,2u));
    }
    free(data);free(source);
}
int main(int argc,char **argv) {
    CHECK(argc<=2);synthetic();if(argc==2) original(argv[1]);
    printf("Native patch literal-pool guard: %u checks passed\n",checks);return 0;
}
