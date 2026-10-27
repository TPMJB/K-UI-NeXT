/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_relocate.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned char image[131072],wire[262216],out[131072],expected[131072];
static uint32_t get32(const unsigned char *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void put32(unsigned char *p,uint32_t v) {for(unsigned i=0;i<4;i++)p[i]=(unsigned char)(v>>(8*i));}
static size_t readfile(const char *path,void *p,size_t cap) {
    FILE *f=fopen(path,"rb");assert(f);size_t n=fread(p,1,cap,f);
    assert(n>0 && n<cap && feof(f) && !ferror(f));assert(!fclose(f));return n;
}
int main(int argc,char **argv) {
    assert(argc==3);size_t n=readfile(argv[1],image,sizeof(image)),w=readfile(argv[2],wire,sizeof(wire));
    uint32_t base=get32(wire+24),count=get32(wire+28),loads[]={0x8c38a880,0x8c500000,0x8cfd0000};
    for(unsigned k=0;k<3;k++) {
        memset(out,0xa5,sizeof(out));memset(expected,0xa5,sizeof(expected));memcpy(expected,image,n);
        for(unsigned i=0;i<count;i++) {
            uint32_t at=get32(wire+72+8*i),v=get32(wire+76+8*i);assert(at<=n-4);
            put32(expected+at,(v&0xe0000000u)+(loads[k]&0x1fffffffu)+(v&0x1fffffffu)-(base&0x1fffffffu));
        }
        assert(!kui_vmu_native_relocate(image,n,wire,w,out,sizeof(out),loads[k]));
        assert(!memcmp(out,expected,sizeof(out)));
    }
    memset(out,0xa5,sizeof(out));image[n-1]^=1;
    assert(kui_vmu_native_relocate(image,n,wire,w,out,sizeof(out),loads[0])==KUI_VMU_NATIVE_RELOCATE_IMAGE);
    for(unsigned i=0;i<sizeof(out);i++) assert(out[i]==0xa5);
    printf("Actual SH worker C relocation: %u pointers at three addresses; tampered image refused before writes\n",count);
    return 0;
}
