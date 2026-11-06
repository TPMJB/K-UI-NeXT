/* SPDX-License-Identifier: GPL-3.0-only */
/* Render the actual terminal pages into an isolated simulated framebuffer.
 * The test never enters the terminal loop or executes console MMIO. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "../src/loader/vc2_ce_vmu_diagnostic.c"

struct kui_armada_ce_maple_traffic_snapshot kui_armada_ce_maple_hook_log;
struct kui_armada_ce_maple_fence_snapshot kui_armada_ce_maple_fence_log;
struct kui_armada_ce_vmu_a2_publish_snapshot kui_armada_ce_vmu_a2_log;
volatile uint32_t kui_armada_ce_maple_traffic_freeze;
volatile uint32_t kui_armada_ce_maple_proof_initial,kui_armada_ce_maple_proof_final;
struct kui_vc2_ce_vmu_prepare_report kui_vc2_ce_vmu_prepare_report;
const struct retail_display_state *kui_vc2_ce_vmu_saved_display(void) {return NULL;}
void kui_vc2_ce_vmu_service_snapshot(uint32_t out[16]) {(void)out;}
#if KUI_VC2_VMU_WRITE_PROBE
void kui_vc2_ce_vmu_write_snapshot(uint32_t out[20]) {(void)out;}
#endif
#if KUI_VC2_VMU_PERSIST
void kui_armada_ce_vmu_runtime_snapshot(uint32_t out[60]) {(void)out;}
void kui_vc2_ce_vmu_loan_snapshot(uint32_t out[8]) {(void)out;}
#endif

static void map_words(uintptr_t address,size_t bytes) {
    assert(mmap((void *)address,bytes,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)address);
}
static void export_frame(const char *path) {
    FILE *f=fopen(path,"wb");assert(f);
    assert(fputs("P6\n640 480\n255\n",f)>=0);
    const uint16_t *pixels=(const void *)(uintptr_t)UINT32_C(0xa5000000);
    for(unsigned i=0;i<640u*480u;i++) {
        uint16_t p=pixels[i];
        uint8_t rgb[3]={(uint8_t)(((p>>11)&31u)*255u/31u),
            (uint8_t)(((p>>5)&63u)*255u/63u),(uint8_t)((p&31u)*255u/31u)};
        assert(fwrite(rgb,1u,3u,f)==3u);
    }
    assert(fclose(f)==0);
    /* Every label/data line has14px glyphs with18px pitch. Neither page
     * may wrap into the launch header or write past its planned21 rows. */
    for(unsigned y=444u;y<480u;y++)for(unsigned x=0;x<640u;x++)
        assert(pixels[y*640u+x]==0x0864u);
}
int main(int argc,char **argv) {
#if KUI_VC2_VMU_WRITE_PROBE || KUI_VC2_VMU_PERSIST
    assert(argc==4);
#else
    assert(argc==3);
#endif
    map_words(UINT32_C(0xa05f8000),4096u);
    map_words(UINT32_C(0xa5000000),640u*480u*2u);
    uint32_t *rows[]={stopped.traffic.counts,stopped.traffic.fault,
        stopped.fence.counts,stopped.fence.before,stopped.fence.held,stopped.fence.after,
        stopped.fence.fault,stopped.a2.counts,stopped.a2.commands,stopped.a2.parent,
        stopped.a2.target,stopped.a2.status,stopped.a2.fault,stopped.top,stopped.owner};
    for(unsigned r=0;r<sizeof(rows)/sizeof(rows[0]);r++)
        for(unsigned c=0;c<5u;c++)rows[r][c]=UINT32_C(0x8c123400)+16u*r+c;
    stopped.top[0]=4u;stopped.top[1]=2u;
    stopped.context[2]=0u;stopped.context[3]=UINT32_C(0x40008000);
    stopped.context[4]=UINT32_C(0x088afe78);stopped.context[5]=UINT32_C(0x03ce7c5c);
    stopped.context[6]=UINT32_C(0x8c1d58a0);stopped.context[7]=UINT32_C(0x105);
    stopped.mmu=UINT32_C(0x7800401);
    stopped.prepare=(struct kui_vc2_ce_vmu_prepare_report){
        UINT32_C(0x31504356),3u,0u,0u,0u,UINT32_C(0x6d9c21a3),
        UINT32_C(0x8cfc4000),UINT32_C(0xb4f90009)};
#if KUI_VC2_VMU_PERSIST
    for(unsigned i=0;i<60u;i++)stopped.runtime[i]=UINT32_C(0xabcd0000)+i;
    for(unsigned i=0;i<8u;i++)stopped.loan[i]=UINT32_C(0x12340000)+i;
    stopped.top[0]=5u;stopped.runtime[8]=7u;
    stopped.runtime[40]=7u;stopped.runtime[41]=UINT32_MAX;
#endif
    first_page();export_frame(argv[1]);
    second_page();export_frame(argv[2]);
#if KUI_VC2_VMU_WRITE_PROBE
    const uint32_t write[20]={0x31505756u,1u,22u,15u,4u,1u,253u,15u,
        0xcafebabeu,1u,1u,1u,1u,1u,0u,1u,0u,0x0200020du,0x000400fdu,524u};
    memcpy(stopped.write,write,sizeof(write));
    write_page();export_frame(argv[3]);
#elif KUI_VC2_VMU_PERSIST
    persist_context_page();export_frame(argv[3]);
#endif
    puts("VC2 diagnostic: all actual pages render within 444px without wrap");
    return 0;
}
