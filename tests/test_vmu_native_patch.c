/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_patch.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"patch guard failed at %u\n",__LINE__);exit(1);}} while(0)
static void word(unsigned char *p,uint32_t v) {for(unsigned i=0;i<4;i++) p[i]=(unsigned char)(v>>(8*i));}
int main(int argc,char **argv) {
    unsigned char data[128];const uint32_t load=0x8c010000u,start=load+64u;
    memset(data,0,sizeof(data));CHECK(kui_vmu_native_patch_no_interior(data,sizeof(data),load,start,12u));
    const uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
    for(unsigned a=0;a<3;a++) for(unsigned target=60;target<=80;target++) {
        memset(data,0,sizeof(data));word(data,(load&0x1fffffffu)+target+aliases[a]);
        CHECK(kui_vmu_native_patch_no_interior(data,sizeof(data),load,start,12u)==!(target>64u && target<76u));
    }
    const uint16_t branches[]={0xa000,0xb000,0x8900,0x8b00,0x8d00,0x8f00};
    for(unsigned b=0;b<6;b++) for(unsigned at=16;at<=112;at+=96) for(unsigned target=60;target<=80;target+=2) {
        memset(data,0,sizeof(data));int delta=((int)target-(int)at-4)/2;
        uint16_t op=branches[b]|((uint16_t)delta&(b<2?4095u:255u));
        data[at]=(unsigned char)op;data[at+1]=(unsigned char)(op>>8);
        CHECK(kui_vmu_native_patch_no_interior(data,sizeof(data),load,start,12u)==!(target>64u && target<76u));
    }
    CHECK(!kui_vmu_native_patch_no_interior(NULL,sizeof(data),load,start,12u));
    CHECK(!kui_vmu_native_patch_no_interior(data,sizeof(data),load,start+1u,12u));
    CHECK(!kui_vmu_native_patch_no_interior(data,sizeof(data),load,load+128u,12u));
    if(argc==2) {
        FILE *f=fopen(argv[1],"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n>0);rewind(f);
        unsigned char *image=malloc((size_t)n);CHECK(image);CHECK(fread(image,1,(size_t)n,f)==(size_t)n);fclose(f);
        /* Locate the unique SDK's exact six displaced instruction words. */
        const unsigned char six[]={0x28,0x90,0xe2,0x63,0x3e,0x02,0xc6,0x12,0x29,0x90,0xe2,0x63};
        unsigned found=0;for(size_t i=0;i+12u<=(size_t)n;i+=2u) if(!memcmp(image+i,six,sizeof(six))) {
            ++found;CHECK(kui_vmu_native_patch_no_interior(image,(size_t)n,load,load+(uint32_t)i,12u));
        }
        CHECK(found==1u);free(image);
    }
    printf("Native patch interior guard: %u checks passed\n",checks);return 0;
}
