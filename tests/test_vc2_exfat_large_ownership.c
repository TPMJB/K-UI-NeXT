/* SPDX-License-Identifier: GPL-3.0-only */
/* Sparse independently authored exFAT volume: the reported128GB device,
 * 128KiB clusters and44 nominal1GiB FAT-chained files in two physical runs.
 * Reuse only fixture formatting helpers; no game payload is allocated/read. */
#define main small_fixture_main
#include "test_vmu_fs_admission.c"
#undef main
#include "kui/vc2_ce_vmu_stage_cache.h"
#include <time.h>
#include <stdlib.h>

#define PERF_CARD UINT32_C(249997312)
#define PERF_PART 2048u
#define PERF_VOLUME (PERF_CARD-PERF_PART)
#define PERF_FAT_OFFSET 32u
#define PERF_HEAP_OFFSET 8192u
#define PERF_SPC 256u
#define PERF_CLUSTERS ((PERF_VOLUME-PERF_HEAP_OFFSET)/PERF_SPC)
#define PERF_FAT_SECTORS ((PERF_CLUSTERS+2u+127u)/128u)
#define PERF_FILES 44u
#define PERF_FILE_CLUSTERS 8192u
#define PERF_HALF (PERF_FILE_CLUSTERS/2u)
static struct {
    struct kui_vmu_fs_input input;
    struct kui_vmu_fs_workspace work;
    struct kui_vmu_persist_profile expected,current;
    struct kui_vc2_ce_vmu_stage_cache cache;
    _Alignas(32) uint8_t boot[24][512],dirs[4][PERF_SPC*512u],bitmap[PERF_SPC*512u],upcase[512u];
    uint32_t fat[PERF_FAT_SECTORS*128u];
    uint64_t calls,sectors,fat_sectors,bitmap_sectors,directory_sectors,other_sectors,unaligned_runs;
    unsigned layout;
} perf;
static uint32_t perf_lba(unsigned c) {return PERF_PART+PERF_HEAP_OFFSET+(c-2u)*PERF_SPC;}
static void perf_alloc(unsigned c) {perf.bitmap[(c-2u)/8u]|=(uint8_t)(1u<<((c-2u)%8u));}
static unsigned perf_file_cluster(unsigned file,unsigned i) {
    if(perf.layout!=0u)return 100u+file*PERF_FILE_CLUSTERS+i;
    return i<PERF_HALF?100u+file*PERF_HALF+i:500000u+file*PERF_HALF+i-PERF_HALF;
}
static void perf_setup(unsigned layout) {
    memset(&perf,0,sizeof(perf));perf.layout=layout;
    perf.input=(struct kui_vmu_fs_input){.card_sectors=PERF_CARD,.partition_first=PERF_PART,
        .partition_end=PERF_CARD,.high_capacity=1u,.pair_path="/KUI/vmu-test/p0001"};
    for(unsigned i=0u;i<16u;i++) {perf.input.cid[i]=(uint8_t)(0x21u+i);perf.input.image_id[i]=(uint8_t)(0x71u+i);}
    uint8_t *b=perf.boot[0];b[0]=0xebu;b[1]=0x76u;b[2]=0x90u;memcpy(b+3u,"EXFAT   ",8u);
    p64(b+64u,PERF_PART);p64(b+72u,PERF_VOLUME);p32(b+80u,PERF_FAT_OFFSET);p32(b+84u,PERF_FAT_SECTORS);
    p32(b+88u,PERF_HEAP_OFFSET);p32(b+92u,PERF_CLUSTERS);p32(b+96u,4u);p32(b+100u,0x12345678u);
    p16(b+104u,0x100u);b[108]=9u;b[109]=8u;b[110]=1u;b[111]=0x80u;p16(b+510u,0xaa55u);
    for(unsigned i=1u;i<=8u;i++)p32(perf.boot[i]+508u,UINT32_C(0xaa550000));
    uint32_t sum=0u;
    for(unsigned sector=0u;sector<11u;sector++)for(unsigned i=0u;i<512u;i++)
        if(sector || (i!=106u && i!=107u && i!=112u))sum=((sum&1u)?UINT32_C(0x80000000):0u)+(sum>>1)+perf.boot[sector][i];
    for(unsigned i=0u;i<512u;i+=4u)p32(perf.boot[11]+i,sum);
    memcpy(perf.boot+12u,perf.boot,12u*512u);
    for(unsigned i=0u;i<128u;i++)p16(perf.upcase+i*2u,upper(i));
    p16(perf.upcase+256u,0xffffu);p16(perf.upcase+258u,65536u-128u);sum=0u;
    for(unsigned i=0u;i<260u;i++)sum=((sum&1u)?UINT32_C(0x80000000):0u)+(sum>>1)+perf.upcase[i];
    perf.dirs[0][0]=0x81u;p32(perf.dirs[0]+20u,2u);p64(perf.dirs[0]+24u,(PERF_CLUSTERS+7u)/8u);
    perf.dirs[0][32u]=0x82u;p32(perf.dirs[0]+36u,sum);p32(perf.dirs[0]+52u,3u);p64(perf.dirs[0]+56u,260u);
    file_entry(perf.dirs[0]+64u,"KUI",16u,5u,PERF_SPC*512u,3u);
    file_entry(perf.dirs[1],"vmu-test",16u,6u,PERF_SPC*512u,3u);
    file_entry(perf.dirs[2],"p0001",16u,7u,PERF_SPC*512u,3u);
    file_entry(perf.dirs[3],"card.vmd",32u,20u,131072u,3u);
    file_entry(perf.dirs[3]+96u,"card.journal",32u,60u,1024u,3u);
    file_entry(perf.dirs[3]+192u,"identity.bin",32u,61u,16u,3u);
    perf.fat[0]=UINT32_C(0xfffffff8);perf.fat[1]=UINT32_MAX;
    for(unsigned c=2u;c<=7u;c++){perf_alloc(c);perf.fat[c]=UINT32_MAX;}
    perf_alloc(20u);perf_alloc(60u);perf_alloc(61u);
    for(unsigned file=0u;file<PERF_FILES;file++) {
        char name[16];snprintf(name,sizeof(name),"game%02u.bin",file);
        file_entry(perf.dirs[0]+160u+96u*file,name,32u,perf_file_cluster(file,0u),UINT64_C(1073741824),layout==2u?3u:1u);
        for(unsigned i=0u;i<PERF_FILE_CLUSTERS;i++) {
            unsigned c=perf_file_cluster(file,i);assert(c<PERF_CLUSTERS+2u);perf_alloc(c);
            perf.fat[c]=i+1u<PERF_FILE_CLUSTERS?perf_file_cluster(file,i+1u):UINT32_MAX;
        }
    }
}
static void perf_sector(uint32_t sector,uint8_t out[512]) {
    ++perf.sectors;memset(out,0,512u);
    if(!sector) {out[450u]=7u;p32(out+454u,PERF_PART);p32(out+458u,PERF_VOLUME);p16(out+510u,0xaa55u);++perf.other_sectors;return;}
    if(sector>=PERF_PART && sector<PERF_PART+24u) {memcpy(out,perf.boot[sector-PERF_PART],512u);++perf.other_sectors;return;}
    if(sector>=PERF_PART+PERF_FAT_OFFSET && sector<PERF_PART+PERF_FAT_OFFSET+PERF_FAT_SECTORS) {
        unsigned at=sector-PERF_PART-PERF_FAT_OFFSET;
        for(unsigned i=0u;i<128u;i++)p32(out+i*4u,perf.fat[at*128u+i]);
        ++perf.fat_sectors;return;
    }
    if(sector>=perf_lba(2u) && sector<perf_lba(2u)+PERF_SPC) {memcpy(out,perf.bitmap+(sector-perf_lba(2u))*512u,512u);++perf.bitmap_sectors;return;}
    if(sector==perf_lba(3u)) {memcpy(out,perf.upcase,512u);++perf.other_sectors;return;}
    for(unsigned d=0u;d<4u;d++)if(sector>=perf_lba(4u+d) && sector<perf_lba(4u+d)+PERF_SPC) {
        memcpy(out,perf.dirs[d]+(sector-perf_lba(4u+d))*512u,512u);++perf.directory_sectors;return;
    }
    if(sector==perf_lba(61u)) {memcpy(out,perf.input.image_id,16u);++perf.other_sectors;return;}
    fprintf(stderr,"unexpected payload read %u\n",sector);abort();
}
static int perf_direct(void *context,uint32_t sector,uint8_t out[512]) {
    (void)context;++perf.calls;perf.unaligned_runs+=!!((uintptr_t)out&31u);perf_sector(sector,out);return 0;
}
static int perf_run(void *context,uint32_t sector,uint32_t count,void *out) {
    (void)context;++perf.calls;perf.unaligned_runs+=!!((uintptr_t)out&31u);
    for(unsigned i=0u;i<count;i++)perf_sector(sector+i,(uint8_t *)out+512u*i);
    return 0;
}
static int perf_cached(void *context,uint32_t sector,uint8_t out[512]) {
    (void)context;return kui_vc2_ce_vmu_stage_cache_read(&perf.cache,&perf.expected,true,false,sector,out,perf_run,NULL);
}
static void perf_reset_counts(void) {
    perf.calls=perf.sectors=perf.fat_sectors=perf.bitmap_sectors=perf.directory_sectors=perf.other_sectors=perf.unaligned_runs=0u;
    memset(&perf.cache,0,sizeof(perf.cache));
}
static void perf_collect(const char *name,bool cached) {
    perf_reset_counts();perf.input.read_sector=cached?perf_cached:perf_direct;
    clock_t start=clock();int result=kui_vmu_fs_collect(&perf.input,&perf.current,&perf.work);
    double seconds=(double)(clock()-start)/CLOCKS_PER_SEC;
    if(result)fprintf(stderr,"result %d detail %u lastLBA%u chain%u\n",result,perf.work.stats.detail,perf.work.stats.last_lba,perf.work.stats.chain_steps);
    assert(!result);assert(perf.work.stats.chain_steps==(perf.layout==2u?0u:PERF_FILES*PERF_FILE_CLUSTERS));
    if(cached) {assert(!memcmp(&perf.expected,&perf.current,sizeof(perf.current)));assert(!perf.unaligned_runs);}else perf.expected=perf.current;
    printf("%s: core_reads=%u chain_steps=%u raw_runs=%llu raw_sectors=%llu fat_sectors=%llu bitmap_sectors=%llu directory_sectors=%llu other_sectors=%llu unaligned_runs=%llu host_cpu_seconds=%.6f\n",
        name,perf.work.stats.reads,perf.work.stats.chain_steps,(unsigned long long)perf.calls,(unsigned long long)perf.sectors,
        (unsigned long long)perf.fat_sectors,(unsigned long long)perf.bitmap_sectors,(unsigned long long)perf.directory_sectors,
        (unsigned long long)perf.other_sectors,(unsigned long long)perf.unaligned_runs,seconds);
}
#ifndef KUI_VC2_EXFAT_LARGE_MAIN
#define KUI_VC2_EXFAT_LARGE_MAIN main
#endif
int KUI_VC2_EXFAT_LARGE_MAIN(void) {
    printf("geometry: card=%u sectors cluster=%u sectors clusters=%u FAT=%u sectors FATwindow=%u;44 x1GiB files\n",
        PERF_CARD,PERF_SPC,PERF_CLUSTERS,PERF_FAT_SECTORS,KUI_VC2_STAGE_FAT_BLOCKS);
    for(unsigned layout=0u;layout<3u;layout++) {
        perf_setup(layout);printf("layout: %s\n",layout==0u?"FAT-chain two runs":layout==1u?"FAT-chain one run":"NoFatChain contiguous");
        perf_collect("uncached",false);perf_collect("stage cache",true);
    }
    puts("PASS representative large exFAT ownership; profile and all live chain steps preserved");return 0;
}
