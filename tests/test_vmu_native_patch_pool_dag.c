/* SPDX-License-Identifier: GPL-3.0-only */
/* Public API only: independently rooted data dependencies must finish before
 * an apparent branch can be ignored. Originals are optional read-only inputs. */
#include "kui/vmu_native_patch.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"pool DAG guard failed at %u\n",__LINE__);exit(1);}} while(0)
enum {N=8192u,A=2048u,B=1792u,FIRST=1024u,GAP=256u,HOOK=5000u,OTHER_HOOK=5400u};
static const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
static void half(uint8_t *p,uint16_t n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static uint16_t h16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static void word(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(8*i));}
static uint16_t bra(uint32_t from,uint32_t to) {
    int32_t d=((int32_t)to-(int32_t)from-4)/2;
    CHECK(d>=-2048 && d<=2047 && !((from|to)&1u));return (uint16_t)(0xa000u|((uint32_t)d&4095u));
}
static uint16_t mov(uint32_t from,uint32_t to,unsigned r) {
    uint32_t base=(from+4u)&~3u;CHECK(to>=base && to-base<=1020u && !((to-base)&3u));
    return (uint16_t)(0xd000u|(r<<8)|((to-base)/4u));
}
static void blank(uint8_t *p) {for(unsigned i=0;i<N;i+=2u)half(p+i,9u);}
static void pool(uint8_t *p,unsigned begin,unsigned count,uint32_t alias) {
    half(p+begin-4u,0x000bu);half(p+begin-2u,9u);
    for(unsigned i=0;i<count;i++) {
        word(p+begin+4u*i,UINT32_C(0x0c020000)|alias|(4u*i));
        half(p+begin-96u+2u*i,mov(begin-96u+2u*i,begin+4u*i,i%15u));
    }
}
static void edge(uint8_t *p,unsigned from,unsigned to,uint32_t alias) {
    word(p+from,UINT32_C(0x0c010000)|alias|bra(from,to));
}
static void dispatcher(uint8_t *p,unsigned begin,uint32_t alias) {
    static const uint16_t code[22]={0x6063u,0x8801u,0x8905u,0x8802u,0x8906u,
        0x8803u,0x8907u,0xa009u,9u,0xd306u,0x432bu,9u,
        0xd305u,0x432bu,9u,0xd305u,0x432bu,9u,0xd304u,0x432bu,9u,0u};
    for(unsigned i=0;i<22u;i++)half(p+begin-44u+2u*i,code[i]);
    for(unsigned i=0;i<4u;i++)word(p+begin+4u*i,UINT32_C(0x0c020000)|alias|(4u*i));
}
static bool admitted(const uint8_t *p,size_t bytes,uint32_t load,bool two) {
    struct kui_vmu_native_patch_window w[]={{load+HOOK,12u},{load+OTHER_HOOK,12u}};
    return kui_vmu_native_patch_no_interiors(p,bytes,load,w,two?2u:1u);
}
static void chain(uint8_t *p,unsigned count,uint32_t alias) {
    blank(p);
    for(unsigned i=0;i<count;i++) {
        unsigned at=FIRST+GAP*i;pool(p,at,2u,alias);
        edge(p,at,i?FIRST+GAP*(i-1u)+2u:HOOK+2u,alias);
    }
}
static void dag(void) {
    uint8_t base[N],data[N];
    CHECK(KUI_VMU_NATIVE_PATCH_POOL_NODES==8u && KUI_VMU_NATIVE_PATCH_POOL_DEPTH==4u);
    for(unsigned l=0;l<3u;l++)for(unsigned v=0;v<3u;v++) {
        uint32_t load=UINT32_C(0x0c010000)|aliases[l],alias=aliases[v];
        for(unsigned depth=1;depth<=5u;depth++) {
            chain(data,depth,alias);CHECK(admitted(data,N,load,false)==(depth<=4u));
        }
        /* Eight-node diamond/fanout succeeds; the ninth independently rooted
         * source exhausts the graph budget instead of silently skipping it. */
        for(unsigned nodes=2;nodes<=9u;nodes++) {
            blank(data);
            for(unsigned i=0;i<nodes;i++) {
                unsigned at=FIRST+GAP*i;pool(data,at,2u,alias);
                edge(data,at,i?FIRST+2u:HOOK+2u,alias);
            }
            CHECK(admitted(data,N,load,false)==(nodes<=8u));
        }
        chain(base,2u,alias);CHECK(admitted(base,N,load,false));
        memcpy(data,base,N);edge(data,FIRST+4u,FIRST+GAP+2u,alias);
        CHECK(!admitted(data,N,load,false)); /* two-node cycle */
        chain(data,1u,alias);edge(data,FIRST+4u,FIRST+2u,alias);
        CHECK(!admitted(data,N,load,false)); /* self dependency */
        chain(data,2u,alias);half(data+FIRST+GAP-4u,9u);
        CHECK(!admitted(data,N,load,false)); /* source has no root */
        /* Pre-prove a subtree, then require it beneath a later root. Cached
         * DONE nodes still contribute their complete dependency height. */
        for(unsigned depth=3u;depth<=4u;depth++) {
            unsigned later=FIRST+8u*GAP;chain(data,depth,alias);pool(data,later,2u,alias);
            edge(data,FIRST+4u,later+2u,alias);edge(data,later,OTHER_HOOK+2u,alias);
            CHECK(admitted(data,N,load,true)==(depth==3u));
        }
        /* Shared DONE leaf: both branches of the diamond reuse one root. */
        chain(data,1u,alias);
        for(unsigned i=1u;i<4u;i++)pool(data,FIRST+GAP*i,2u,alias);
        edge(data,FIRST+GAP,FIRST+2u,alias);edge(data,FIRST+2u*GAP,FIRST+2u,alias);
        edge(data,FIRST+3u*GAP,FIRST+GAP+2u,alias);
        edge(data,FIRST+3u*GAP+4u,FIRST+2u*GAP+2u,alias);
        CHECK(admitted(data,N,load,false));
        /* The reviewed exact tail dispatcher independently roots the second
         * pool. An arbitrary RAM run ending in a JMP is insufficient. */
        blank(base);pool(base,A,23u,alias);edge(base,A+48u,HOOK+2u,alias);
        dispatcher(base,B,alias);edge(base,B,A+68u,alias);CHECK(admitted(base,N,load,false));
        for(unsigned i=0;i<22u;i++)for(unsigned bit=0;bit<16u;bit++) {
            memcpy(data,base,N);half(data+B-44u+2u*i,
                (uint16_t)(h16(data+B-44u+2u*i)^(UINT32_C(1)<<bit)));
            CHECK(!admitted(data,N,load,false));
        }
        for(unsigned i=0;i<23u;i++) {
            memcpy(data,base,N);half(data+A-96u+2u*i,9u);
            CHECK(!admitted(data,N,load,false));
        }
        for(unsigned i=0;i<4u;i++) {
            const unsigned refs[]={B-26u,B-20u,B-14u,B-8u};
            memcpy(data,base,N);half(data+refs[i],9u);CHECK(!admitted(data,N,load,false));
        }
        /* These transports remain forbidden even when their source is a
         * fully rooted pool. No dependency can discharge an actual pointer. */
        for(unsigned a=0;a<3u;a++) {
            memcpy(data,base,N);word(data+B+4u,(UINT32_C(0x0c010000)+A+2u)|aliases[a]);
            CHECK(!admitted(data,N,load,false));
            memcpy(data,base,N);word(data+B+4u,(UINT32_C(0x0c010000)+HOOK+2u)|aliases[a]);
            CHECK(!admitted(data,N,load,false));
        }
        memcpy(data,base,N);word(data+B+4u,UINT32_C(0x0c010000)|alias|
            (0x9000u|((A+2u-(B+4u+4u))/2u)));
        CHECK(!admitted(data,N,load,false));
        memcpy(data,base,N);word(data+B+4u,UINT32_C(0x0c010000)|alias|
            (0xc700u|((A-((B+4u+4u)&~3u))/4u)));
        CHECK(!admitted(data,N,load,false));
        memcpy(data,base,N);half(data+512u,bra(512u,A+2u));CHECK(!admitted(data,N,load,false));
        struct kui_vmu_native_patch_window overlap[]={{load+HOOK,12u},{load+B,4u}};
        CHECK(!kui_vmu_native_patch_no_interiors(base,N,load,overlap,2u));
    }
}
static void original(const char *path) {
    FILE *f=fopen(path,"rb");CHECK(f);CHECK(!fseek(f,0,SEEK_END));long n=ftell(f);CHECK(n==6751168);rewind(f);
    uint8_t *p=malloc((size_t)n),*copy=malloc((size_t)n);CHECK(p && copy);
    CHECK(fread(p,1,(size_t)n,f)==(size_t)n);CHECK(!fclose(f));
    const uint32_t load=0x8c010000u,root=0x5f6c40u,leaf=0x5f6894u;
    const struct kui_vmu_native_patch_window windows[]={{0x8c605fc8u,20u}};
    CHECK(h16(p+root-4u)==0x000bu && h16(p+root-2u)==0x6ef6u);
    CHECK(h16(p+leaf-6u)==0x432bu && h16(p+leaf-4u)==9u && !h16(p+leaf-2u));
    for(unsigned a=0;a<3u;a++)CHECK(kui_vmu_native_patch_no_interiors(p,(size_t)n,
        (load&0x1fffffffu)|aliases[a],windows,1u));
    for(unsigned i=0;i<22u;i++)for(unsigned bit=0;bit<16u;bit++) {
        memcpy(copy,p,(size_t)n);half(copy+leaf-44u+2u*i,
            (uint16_t)(h16(copy+leaf-44u+2u*i)^(UINT32_C(1)<<bit)));
        CHECK(!kui_vmu_native_patch_no_interiors(copy,(size_t)n,load,windows,1u));
    }
    for(unsigned i=0;i<23u;i++) {
        memcpy(copy,p,(size_t)n);unsigned refs=0u;
        for(uint32_t at=0;at+2u<=root-4u;at+=2u) {
            uint16_t op=h16(p+at);
            if((op&0xf000u)==0xd000u && (((load+at+4u)&~3u)+4u*(op&255u))==load+root+4u*i) {
                half(copy+at,9u);++refs;
            }
        }
        CHECK(refs>0u);CHECK(!kui_vmu_native_patch_no_interiors(copy,(size_t)n,load,windows,1u));
    }
    for(unsigned a=0;a<3u;a++) {
        memcpy(copy,p,(size_t)n);word(copy+leaf+4u,((load+root+2u)&0x1fffffffu)|aliases[a]);
        CHECK(!kui_vmu_native_patch_no_interiors(copy,(size_t)n,load,windows,1u));
    }
    free(copy);free(p);
}
int main(int argc,char **argv) {
    CHECK(argc<=2);dag();if(argc==2)original(argv[1]);
    printf("Native patch acyclic pool graph: %u checks passed\n",checks);return 0;
}
