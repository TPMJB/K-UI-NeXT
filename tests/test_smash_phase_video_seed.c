/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_phase_video.h"
#include "kui/smash_phase_trace.h"
#include "kui/retail_image.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(v) do {++checks;if(!(v)){fprintf(stderr,"video seed check failed at %u\n",__LINE__);exit(1);}}while(0)
void kui_smash_phase_clock_read(struct kui_smash_phase_stamp *s) {memset(s,0,sizeof(*s));}
static struct kui_smash_phase_video_seed blank(void) {
    return (struct kui_smash_phase_video_seed){.magic0=KUI_SMASH_PHASE_VIDEO_MAGIC0,
        .magic1=KUI_SMASH_PHASE_VIDEO_MAGIC1,.version=1u,.bytes=80u};
}
static void checksum(struct kui_smash_phase_video_seed *s) {s->crc32=0u;s->crc32=kui_retail_crc32(0u,s,sizeof(*s));}
static uint8_t *payload(const char *path,size_t *bytes) {
    FILE *file=fopen(path,"rb");CHECK(file);CHECK(!fseek(file,0,SEEK_END));long n=ftell(file);CHECK(n>64);
    CHECK(!fseek(file,64,SEEK_SET));*bytes=(size_t)n-64u;uint8_t *data=malloc(*bytes);CHECK(data);
    CHECK(fread(data,1u,*bytes,file)==*bytes);CHECK(!fclose(file));return data;
}
int main(int argc,char **argv) {
    uint32_t regs[14]={0x800005u,9u,0u,0u,319u|(479u<<10)|(1u<<20),0x150104u,
        0x100u,0x7e0345u,0x240204u,0x20c0359u,0u,172u,0x280028u,0u};
    struct kui_smash_phase_video_seed s=blank();CHECK(kui_smash_phase_video_blank(&s));CHECK(!kui_smash_phase_video_valid(&s));
    CHECK(kui_smash_phase_video_capture(&s,regs));CHECK(kui_smash_phase_video_valid(&s));CHECK(!kui_smash_phase_video_blank(&s));
    CHECK(!memcmp(s.regs,regs,sizeof(regs)));
    for(unsigned bit=0u;bit<80u*8u;bit++) {
        struct kui_smash_phase_video_seed b=s;((uint8_t *)&b)[bit/8u]^=1u<<(bit%8u);
        CHECK(!kui_smash_phase_video_valid(&b));
        b=blank();((uint8_t *)&b)[bit/8u]^=1u<<(bit%8u);CHECK(!kui_smash_phase_video_blank(&b));
    }
    for(unsigned i=0u;i<7u;i++) {
        struct kui_smash_phase_video_seed b=s;
        if(i==0u)b.regs[0]&=~1u;
        if(i==1u)b.regs[0]^=4u;
        if(i==2u)b.regs[2]=0x200000u;
        if(i==3u)b.regs[4]^=1u;
        if(i==4u)b.regs[10]|=8u;
        if(i==5u)b.regs[10]|=0x100u;
        if(i==6u)b.reserved=1u;
        checksum(&b);CHECK(!kui_smash_phase_video_valid(&b));
    }
    regs[0]=5u;regs[3]=1280u;regs[4]=319u|(239u<<10)|(321u<<20);regs[6]=0x150u;
    CHECK(kui_smash_phase_video_capture(&s,regs));CHECK(kui_smash_phase_video_valid(&s));
    regs[6]=0x190u;CHECK(kui_smash_phase_video_capture(&s,regs));CHECK(kui_smash_phase_video_valid(&s));
    _Alignas(4) uint8_t outer[64]={0},child[2048]={0};uint32_t flags=99u;
    CHECK(kui_smash_phase_pair_profile(outer,sizeof(outer),child,sizeof(child),64u,&flags));CHECK(flags==0u);
    uint32_t profile[4]={KUI_SMASH_PHASE_OUTER_WORDS};memcpy(outer+16u,profile,sizeof(profile));
    CHECK(!kui_smash_phase_pair_profile(outer,sizeof(outer),child,sizeof(child),64u,&flags));
    struct kui_smash_phase_trace *trace=(void *)(child+64u+304u+368u);kui_smash_phase_trace_init(trace);
    struct kui_smash_phase_video_seed *video=(void *)((uint8_t *)trace+sizeof(*trace));*video=blank();
    CHECK(kui_smash_phase_pair_profile(outer,sizeof(outer),child,sizeof(child),64u,&flags));CHECK(flags==7u);
    for(unsigned bit=0u;bit<sizeof(profile)*8u;bit++) {
        outer[16u+bit/8u]^=1u<<(bit%8u);
        CHECK(!kui_smash_phase_pair_profile(outer,sizeof(outer),child,sizeof(child),64u,&flags));
        outer[16u+bit/8u]^=1u<<(bit%8u);
    }
    memset(outer,0,sizeof(outer));CHECK(!kui_smash_phase_pair_profile(outer,sizeof(outer),child,sizeof(child),64u,&flags));
    memcpy(outer+16u,profile,sizeof(profile));memcpy(outer+32u,profile,sizeof(profile));
    CHECK(!kui_smash_phase_pair_profile(outer,sizeof(outer),child,sizeof(child),64u,&flags));memset(outer+32u,0,16u);
    CHECK(!kui_smash_phase_pair_profile(outer,sizeof(outer),child,1760u,64u,&flags));
    CHECK(!kui_smash_phase_pair_profile(outer,sizeof(outer),child,sizeof(child),SIZE_MAX,&flags));
    CHECK(!kui_smash_phase_pair_profile(outer,sizeof(outer),child+1u,sizeof(child)-1u,64u,&flags));
    trace->count=1u;CHECK(!kui_smash_phase_pair_profile(outer,sizeof(outer),child,sizeof(child),64u,&flags));trace->count=0u;
    *video=s;CHECK(!kui_smash_phase_pair_profile(outer,sizeof(outer),child,sizeof(child),64u,&flags));*video=blank();
    struct kui_smash_phase_video_seed temp=*video;memset(video,0,sizeof(*video));memcpy((uint8_t *)video+4u,&temp,sizeof(temp));
    CHECK(!kui_smash_phase_pair_profile(outer,sizeof(outer),child,sizeof(child),64u,&flags));
    uint32_t actual_flags=0u;
    if(argc==3) {
        size_t on=0u,cn=0u;uint8_t *ob=payload(argv[1],&on),*cb=payload(argv[2],&cn);size_t native_at=SIZE_MAX;
        for(size_t at=0u;at<=cn && 16u<=cn-at;at+=4u) {
            const uint32_t *h=(const void *)(cb+at);
            if(h[0]==0x5349554bu && h[1]==0x31554e4du && h[2]==1u && h[3]==304u) {CHECK(native_at==SIZE_MAX);native_at=at;}
        }
        CHECK(native_at!=SIZE_MAX);
        CHECK(kui_smash_phase_pair_profile(ob,on,cb,cn,native_at,&actual_flags));CHECK(actual_flags==7u);
        free(cb);free(ob);
    }
    printf("{\"status\":\"PASS\",\"checks\":%u,\"video_single_bit_refusals\":640,\"blank_single_bit_refusals\":640,\"profile_single_bit_refusals\":128,\"normal_and_diagnostic_pairs\":true,\"actual_pair_flags\":%u}\n",checks,actual_flags);
    return 0;
}
