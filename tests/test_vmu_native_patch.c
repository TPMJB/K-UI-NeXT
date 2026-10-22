/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_patch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"patch guard failed at %u\n",__LINE__);exit(1);}} while(0)
static void word(unsigned char *p,uint32_t v) {for(unsigned i=0;i<4;i++) p[i]=(unsigned char)(v>>(8*i));}
static void single_matches_batch(const void *data,size_t bytes,uint32_t load,uint32_t start,uint32_t span) {
    const struct kui_vmu_native_patch_window w={start,span};
    CHECK(kui_vmu_native_patch_no_interior(data,bytes,load,start,span)==
        kui_vmu_native_patch_no_interiors(data,bytes,load,&w,1u));
}
static void veneers(void) {
    const uint32_t targets[]={UINT32_C(0x8c004000),UINT32_C(0x8c38a914),
        UINT32_C(0xac38a914),UINT32_C(0xacfffffe)};
    for(unsigned offset=0;offset<512u;offset+=2u) for(unsigned t=0;t<4u;t++) {
        uint32_t address=UINT32_C(0x8c010000)+offset;
        uint16_t patch[6];CHECK(kui_vmu_native_patch_encode(address,targets[t],patch));
        CHECK((patch[0]&UINT16_C(0xff00))==UINT16_C(0xd000));
        uint32_t literal=((address+4u)&~3u)+4u*(patch[0]&255u);
        CHECK(!(literal&3u) && literal>=address+6u && literal+4u<=address+12u);
        unsigned word_index=(literal-address)/2u;
        uint32_t loaded=(uint32_t)patch[word_index]|(uint32_t)patch[word_index+1u]<<16;
        CHECK(loaded==targets[t]);CHECK(patch[1]==UINT16_C(0x402b));CHECK(patch[2]==9u);
        if(!(address&3u)) {
            const uint16_t old[6]={UINT16_C(0xd001),UINT16_C(0x402b),9u,9u,
                (uint16_t)targets[t],(uint16_t)(targets[t]>>16)};
            CHECK(!memcmp(patch,old,sizeof(old)));
        }
    }
    const uint32_t invalid_address[]={0u,UINT32_C(0x8c010001),UINT32_C(0x8cfffff6),UINT32_MAX};
    const uint32_t invalid_target[]={0u,UINT32_C(0x0c38a914),UINT32_C(0x8c38a915),
        UINT32_C(0x8bfffffe),UINT32_C(0xad000000),UINT32_C(0xcc38a914)};
    uint16_t out[6],before[6];memset(out,0xa5,sizeof(out));memcpy(before,out,sizeof(out));
    for(unsigned i=0;i<sizeof(invalid_address)/sizeof(invalid_address[0]);i++) {
        CHECK(!kui_vmu_native_patch_encode(invalid_address[i],targets[0],out));CHECK(!memcmp(out,before,sizeof(out)));
    }
    for(unsigned i=0;i<sizeof(invalid_target)/sizeof(invalid_target[0]);i++) {
        CHECK(!kui_vmu_native_patch_encode(UINT32_C(0x8c010000),invalid_target[i],out));CHECK(!memcmp(out,before,sizeof(out)));
    }
    CHECK(!kui_vmu_native_patch_encode(UINT32_C(0x8c010000),targets[0],NULL));
}
int main(int argc,char **argv) {
    veneers();
    unsigned char data[128];const uint32_t load=0x8c010000u,start=load+64u;
    memset(data,0,sizeof(data));CHECK(kui_vmu_native_patch_no_interior(data,sizeof(data),load,start,12u));
    const uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
    for(unsigned a=0;a<3;a++) for(unsigned target=60;target<=80;target++) {
        memset(data,0,sizeof(data));word(data,(load&0x1fffffffu)+target+aliases[a]);
        CHECK(kui_vmu_native_patch_no_interior(data,sizeof(data),load,start,12u)==!(target>64u && target<76u));
        single_matches_batch(data,sizeof(data),load,start,12u);
    }
    const uint16_t branches[]={0xa000,0xb000,0x8900,0x8b00,0x8d00,0x8f00};
    for(unsigned b=0;b<6;b++) for(unsigned at=16;at<=112;at+=96) for(unsigned target=60;target<=80;target+=2) {
        memset(data,0,sizeof(data));int delta=((int)target-(int)at-4)/2;
        uint16_t op=branches[b]|((uint16_t)delta&(b<2?4095u:255u));
        data[at]=(unsigned char)op;data[at+1]=(unsigned char)(op>>8);
        CHECK(kui_vmu_native_patch_no_interior(data,sizeof(data),load,start,12u)==!(target>64u && target<76u));
        single_matches_batch(data,sizeof(data),load,start,12u);
    }
    CHECK(!kui_vmu_native_patch_no_interior(NULL,sizeof(data),load,start,12u));
    CHECK(!kui_vmu_native_patch_no_interior(data,sizeof(data),load,start+1u,12u));
    CHECK(!kui_vmu_native_patch_no_interior(data,sizeof(data),load,load+128u,12u));
    single_matches_batch(NULL,sizeof(data),load,start,12u);
    single_matches_batch(data,sizeof(data),load,start+1u,12u);
    single_matches_batch(data,sizeof(data),load,load+128u,12u);

    const struct kui_vmu_native_patch_window pair[]={{load+40u,12u},{load+88u,12u}};
    memset(data,0,sizeof(data));
    CHECK(kui_vmu_native_patch_no_interiors(data,sizeof(data),load,pair,2u));
    for(unsigned a=0;a<3u;a++) for(unsigned target=36u;target<=104u;target++) {
        memset(data,0,sizeof(data));word(data,(load&0x1fffffffu)+target+aliases[a]);
        bool inside=(target>40u && target<52u) || (target>88u && target<100u);
        CHECK(kui_vmu_native_patch_no_interiors(data,sizeof(data),load,pair,2u)==!inside);
    }
    for(unsigned b=0;b<6u;b++) for(unsigned at=16u;at<=112u;at+=96u)
        for(unsigned target=36u;target<=104u;target+=2u) {
            memset(data,0,sizeof(data));int delta=((int)target-(int)at-4)/2;
            uint16_t op=branches[b]|((uint16_t)delta&(b<2u?4095u:255u));
            data[at]=(unsigned char)op;data[at+1u]=(unsigned char)(op>>8);
            bool inside=(target>40u && target<52u) || (target>88u && target<100u);
            CHECK(kui_vmu_native_patch_no_interiors(data,sizeof(data),load,pair,2u)==!inside);
        }
    memset(data,0,sizeof(data));
    struct kui_vmu_native_patch_window many[9];
    for(unsigned i=0;i<9u;i++) many[i]=(struct kui_vmu_native_patch_window){load+16u+i*8u,4u};
    CHECK(kui_vmu_native_patch_no_interiors(data,sizeof(data),load,many,8u));
    CHECK(!kui_vmu_native_patch_no_interiors(data,sizeof(data),load,many,9u));
    CHECK(!kui_vmu_native_patch_no_interiors(data,sizeof(data),load,many,0u));
    CHECK(!kui_vmu_native_patch_no_interiors(data,sizeof(data),load,NULL,1u));
    CHECK(!kui_vmu_native_patch_no_interiors(NULL,sizeof(data),load,pair,2u));
    CHECK(!kui_vmu_native_patch_no_interiors(data,1u,load,pair,2u));
    CHECK(!kui_vmu_native_patch_no_interiors(data,(size_t)0xc00001u,load,pair,2u));
    CHECK(!kui_vmu_native_patch_no_interiors(data,sizeof(data),load+1u,pair,2u));
    CHECK(!kui_vmu_native_patch_no_interiors(data,sizeof(data),0x8c00ffffu,pair,2u));
    CHECK(!kui_vmu_native_patch_no_interiors(data,sizeof(data),0x8cfffff0u,pair,2u));
    for(unsigned a=0;a<3u;a++) for(unsigned b=0;b<3u;b++) {
        struct kui_vmu_native_patch_window w[]={
            {(load&0x1fffffffu)+40u+aliases[a],12u},
            {(load&0x1fffffffu)+88u+aliases[b],12u}};
        CHECK(kui_vmu_native_patch_no_interiors(data,sizeof(data),load,w,2u));
        struct kui_vmu_native_patch_window reversed[]={w[1],w[0]};
        CHECK(kui_vmu_native_patch_no_interiors(data,sizeof(data),load,reversed,2u));
        for(unsigned alias_load=0;alias_load<3u;alias_load++)
            CHECK(kui_vmu_native_patch_no_interiors(data,sizeof(data),
                (load&0x1fffffffu)+aliases[alias_load],w,2u));
        w[1].begin=(load&0x1fffffffu)+40u+aliases[b];
        CHECK(!kui_vmu_native_patch_no_interiors(data,sizeof(data),load,w,2u));
        w[1].begin+=10u;
        CHECK(!kui_vmu_native_patch_no_interiors(data,sizeof(data),load,w,2u));
        w[1].begin+=2u;
        CHECK(kui_vmu_native_patch_no_interiors(data,sizeof(data),load,w,2u));
    }
    const struct kui_vmu_native_patch_window invalid[]={
        {load+39u,12u},{load+40u,11u},{load+40u,0u},{load+40u,1u},
        {load+128u,12u},{load+120u,12u},{load-2u,12u},
        {0xcc010040u,12u},{UINT32_MAX,12u},{load+40u,UINT32_MAX}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        struct kui_vmu_native_patch_window w[]={pair[0],invalid[i]};
        CHECK(!kui_vmu_native_patch_no_interiors(data,sizeof(data),load,w,2u));
        w[0]=invalid[i];w[1]=pair[1];
        CHECK(!kui_vmu_native_patch_no_interiors(data,sizeof(data),load,w,2u));
    }
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
        unsigned char *image=malloc((size_t)n);CHECK(image);CHECK(fread(image,1,(size_t)n,f)==(size_t)n);fclose(f);
        /* Locate the unique SDK's exact six displaced instruction words. */
        const unsigned char six[]={0x28,0x90,0xe2,0x63,0x3e,0x02,0xc6,0x12,0x29,0x90,0xe2,0x63};
        unsigned found=0;uint32_t submit=0u;
        for(size_t i=0;i+12u<=(size_t)n;i+=2u) if(!memcmp(image+i,six,sizeof(six))) {
            ++found;submit=load+(uint32_t)i;
            CHECK(kui_vmu_native_patch_no_interior(image,(size_t)n,load,submit,12u));
            single_matches_batch(image,(size_t)n,load,submit,12u);
        }
        CHECK(found==1u);
        /* Optional actual Power Stone 2 USA evidence: the two reviewed
         * tail32 allocator entries and the unique discovered submit window.
         * Mutations affect only this malloc copy, never the supplied file. */
        const uint32_t init=0x8c2b15c8u,release=init+0x1b0u;
        CHECK((size_t)n>release-load+20u);
        CHECK(image[init-load]==0xe6u && image[init-load+1u]==0x2fu);
        CHECK(image[release-load]==0x02u && image[release-load+1u]==0x00u);
        const struct kui_vmu_native_patch_window actual[]={
            {submit,12u},{init&0x1fffffffu,12u},{release+0x20000000u,20u}};
        CHECK(kui_vmu_native_patch_no_interiors(image,(size_t)n,load,actual,3u));
        unsigned char saved[4];memcpy(saved,image,sizeof(saved));
        for(unsigned i=0;i<3u;i++) {
            word(image,actual[i].begin+2u);
            CHECK(!kui_vmu_native_patch_no_interiors(image,(size_t)n,load,actual,3u));
            memcpy(image,saved,sizeof(saved));
        }
        CHECK(kui_vmu_native_patch_no_interiors(image,(size_t)n,load,actual,3u));free(image);
    }
    printf("Native patch interior guard: %u checks passed\n",checks);return 0;
}
