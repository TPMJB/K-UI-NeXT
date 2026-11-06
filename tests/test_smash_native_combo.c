/* SPDX-License-Identifier: GPL-3.0-only */
/* Optional original MDE is read-only external evidence, never a repo fixture. */
#include "kui/smash_native_combo.h"
#include "kui/smash_native_child.h"
#include "kui/vmu_native_discovery.h"
#include "kui/vmu_native_exit.h"
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_layout.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Smash combo check failed at line %u\n",__LINE__);exit(1);}}while(0)
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void rejected(const void *p,size_t n,uint32_t load,const uint8_t *sha,
    const struct kui_vmu_native_patch_window *w,size_t count) {
    struct kui_smash_native_combo_plan out,before;memset(&out,0x5c,sizeof(out));before=out;
    CHECK(!kui_smash_native_combo_plan(p,n,load,sha,w,count,&out));
    CHECK(!memcmp(&out,&before,sizeof(out)));
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *file=fopen(argv[1],"rb");CHECK(file);
    CHECK(!fseek(file,0,SEEK_END));CHECK(ftell(file)==KUI_SMASH_NATIVE_ENGINE_BYTES);
    CHECK(!fseek(file,0,SEEK_SET));size_t n=KUI_SMASH_NATIVE_ENGINE_BYTES;
    uint8_t *p=malloc(n),*before=malloc(n);CHECK(p && before);
    CHECK(fread(p,1,n,file)==n);CHECK(!fclose(file));memcpy(before,p,n);
    uint8_t sha[32];struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,p,n);kui_sha256_digest(&h,sha);
    CHECK(kui_smash_native_child_engine_owner(n,sha));
    struct kui_vmu_native_discovery d;struct kui_vmu_native_layout layout;
    struct kui_vmu_native_heap heap;struct kui_vmu_native_exit exits;
    CHECK(kui_vmu_native_discover_layout(p,n,0x8c010000u,&d,&layout));
    CHECK(kui_vmu_native_heap_discover(p,n,0x8c010000u,&heap)==KUI_VMU_NATIVE_HEAP_FOUND);
    CHECK(kui_vmu_native_exit_discover(p,n,0x8c010000u,&exits));
    CHECK(d.family==KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 && layout.rx_layout==KUI_VMU_NATIVE_RX_UNIFORM48);
    uint32_t pc=layout.initializer_wrapper+42u,off=pc-0x8c010000u;
    uint16_t op=(uint16_t)(p[off]|p[off+1u]<<8);CHECK((op&0xff00u)==0xd300u);
    uint32_t slot=((pc+4u)&~3u)+4u*(op&255u);
    CHECK(get32(p+slot-0x8c010000u)==layout.initializer);
    struct kui_vmu_native_patch_window w[12]={
        {d.submit,12u},{heap.initializer,12u},{heap.release,20u},{heap.initializer_slot,4u},
        {d.completion_slot,4u},{layout.initializer,12u},{layout.initializer_wrapper,56u},{slot,4u}};
    size_t count=8u;CHECK(exits.count<=KUI_VMU_NATIVE_EXIT_ENTRIES);
    for(unsigned i=0;i<exits.count;i++)w[count++]=(struct kui_vmu_native_patch_window){exits.entries[i],kui_vmu_native_exit_window(&exits,i)};
    struct kui_smash_native_combo_plan out;
    CHECK(kui_smash_native_combo_plan(p,n,0x8c010000u,sha,w,count,&out));
    CHECK(out.address==0x8c01048eu && out.original==0xa1f8u && out.replacement==0xa1f4u);
    CHECK(out.call==0x8c01087au && out.reset==0x8c0117f0u);CHECK(!memcmp(p,before,n));
    for(unsigned i=0;i<32u;i++){sha[i]^=1u;rejected(p,n,0x8c010000u,sha,w,count);sha[i]^=1u;}
    const uint32_t ranges[][2]={{0x480u,18u},{0x87au,8u},{0x520u,8u}};
    for(unsigned r=0;r<3u;r++)for(unsigned i=0;i<ranges[r][1];i++)for(unsigned bit=0;bit<8u;bit++) {
        uint32_t off=ranges[r][0]+i;p[off]^=(uint8_t)(1u<<bit);
        rejected(p,n,0x8c010000u,sha,w,count);p[off]^=(uint8_t)(1u<<bit);
    }
    rejected(NULL,n,0x8c010000u,sha,w,count);rejected(p,n-1u,0x8c010000u,sha,w,count);
    rejected(p,n+1u,0x8c010000u,sha,w,count);rejected(p,n,0xac010000u,sha,w,count);
    rejected(p,n,0x8c010000u,NULL,w,count);rejected(p,n,0x8c010000u,sha,NULL,1u);
    rejected(p,n,0x8c010000u,sha,w,17u);
    if(SIZE_MAX>UINT32_MAX)rejected(p,n+(UINT64_C(1)<<32),0x8c010000u,sha,w,count);
    rejected((void *)(uintptr_t)(UINTPTR_MAX-8u),n,0x8c010000u,sha,w,count);
    rejected(p,n,0x8c010000u,(void *)(uintptr_t)(UINTPTR_MAX-8u),w,count);
    rejected(p,n,0x8c010000u,sha,(void *)(uintptr_t)(UINTPTR_MAX-3u),1u);
    CHECK(!kui_smash_native_combo_plan(p,n,0x8c010000u,sha,w,count,NULL));
    CHECK(!kui_smash_native_combo_plan(p,n,0x8c010000u,sha,w,count,(void *)(uintptr_t)(UINTPTR_MAX-7u)));
    CHECK(!kui_smash_native_combo_plan(p,n,0x8c010000u,sha,w,count,(void *)(p+n-8u)));
    CHECK(!memcmp(p,before,n));
    CHECK(!kui_smash_native_combo_plan(p,n,0x8c010000u,sha,w,count,(void *)(sha+1u)));
    CHECK(!kui_smash_native_combo_plan(p,n,0x8c010000u,sha,w,count,(void *)w));
    const uint32_t aliases[]={0x0c01048eu,0x8c01048eu,0xac01048eu};
    for(unsigned i=0;i<3u;i++) {
        struct kui_vmu_native_patch_window bad={aliases[i],2u};
        rejected(p,n,0x8c010000u,sha,&bad,1u);bad.begin-=2u;bad.bytes=4u;
        rejected(p,n,0x8c010000u,sha,&bad,1u);bad.bytes=2u;
        CHECK(kui_smash_native_combo_plan(p,n,0x8c010000u,sha,&bad,1u,&out));
    }
    const struct kui_vmu_native_patch_window invalid[]={{0x8c010400u,0u},{0x8c010401u,2u},
        {0xec010400u,2u},{0x8c00fffeu,2u},{0x8c010400u,UINT32_MAX},{0x8c1d33e0u,2u}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)rejected(p,n,0x8c010000u,sha,&invalid[i],1u);
    CHECK(!memcmp(p,before,n));free(p);free(before);
    printf("Smash source-owned combo plan: %u checks, exact owner/windows, SDK disjointness, malformed/alias refusal\n",checks);
    return 0;
}
