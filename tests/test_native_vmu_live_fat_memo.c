/* SPDX-License-Identifier: GPL-3.0-only */
/* Real large exFAT parser, namespace and every live FAT link. Only compare
 * the old/native read adapter against reuse of already verified windows. */
#define KUI_VC2_EXFAT_LARGE_MAIN unused_large_exfat_suite
#include "test_vc2_exfat_large_ownership.c"
#include "kui/smash_fat_read_cache.h"
static struct kui_smash_fat_read_cache native_fat;
static unsigned memo_fat;
extern int reference_cache_metadata_read(struct kui_vc2_ce_vmu_stage_cache *,
    const struct kui_vmu_persist_profile *,bool,uint64_t,void *,kui_vc2_stage_read_run,void *);
static int native_one(void *context,uint32_t lba,uint8_t out[512]) {
    (void)context;
    if(!memo_fat)return reference_cache_metadata_read(&perf.cache,&perf.expected,true,
        lba,out,perf_run,NULL);
    return kui_vc2_ce_vmu_stage_cache_metadata_read(&perf.cache,&perf.expected,true,
        lba,out,perf_run,NULL);
}
static int native_window(void *context,uint32_t lba,uint32_t count,uint8_t *out,uint32_t *verified) {
    (void)context;*verified=0u;
    if(perf_run(NULL,lba,count,out))return -1;
    *verified=count;
    if(memo_fat)assert(!kui_vc2_ce_vmu_stage_cache_admission_store(&perf.cache,
        &perf.expected,lba,count,out));
    return 0;
}
static int native_read(void *context,uint32_t lba,uint8_t out[512]) {
    (void)context;
    if(memo_fat) {
        int hit=kui_vc2_ce_vmu_stage_cache_admission_peek(&perf.cache,&perf.expected,lba,out);
        if(hit)return hit>0?0:-1;
    }
    const struct kui_smash_fat_read_ops ops={NULL,native_one,native_window};
    return kui_smash_fat_cache_read(&native_fat,&ops,lba,out);
}
struct native_counts {uint64_t commands,sectors;struct kui_vmu_fs_stats stats;};
static struct native_counts collect_native(unsigned enable) {
    perf_reset_counts();memset(&native_fat,0,sizeof(native_fat));memo_fat=enable;
    assert(kui_smash_fat_cache_configure(&native_fat,perf.expected.card_sectors,
        perf.expected.partition_first,perf.expected.partition_end));
    perf.input.read_sector=native_read;
    assert(!kui_vmu_fs_collect(&perf.input,&perf.current,&perf.work));
    assert(!memcmp(&perf.expected,&perf.current,sizeof(perf.current)) && !perf.unaligned_runs);
    return (struct native_counts){perf.calls,perf.sectors,perf.work.stats};
}
static struct native_counts alternating(unsigned enable) {
    perf_reset_counts();memset(&native_fat,0,sizeof(native_fat));memo_fat=enable;
    assert(kui_smash_fat_cache_configure(&native_fat,perf.expected.card_sectors,
        perf.expected.partition_first,perf.expected.partition_end));
    uint8_t out[512],expected[512];
    assert(!native_read(NULL,PERF_PART,out)); /* The actual live VBR selects FAT windows. */
    for(unsigned sector=0u;sector<256u;sector++) {
        assert(!native_read(NULL,perf_lba(4u)+sector,out));
        assert(!memcmp(out,perf.dirs[0]+sector*512u,512u));
    }
    for(unsigned i=0u;i<64u;i++) {
        uint32_t sector=32u+16u*(i&3u);
        assert(!native_read(NULL,PERF_PART+PERF_FAT_OFFSET+sector,out));
        for(unsigned j=0u;j<128u;j++)p32(expected+j*4u,perf.fat[sector*128u+j]);
        assert(!memcmp(out,expected,512u));
    }
    unsigned before=(unsigned)perf.calls;
    for(unsigned sector=0u;sector<256u;sector++) {
        assert(!native_read(NULL,perf_lba(4u)+sector,out));
        assert(!memcmp(out,perf.dirs[0]+sector*512u,512u));
    }
    assert(perf.calls==before); /* A FAT walk cannot evict this metadata working set. */
    return (struct native_counts){perf.calls,perf.sectors,{0}};
}
int main(void) {
    for(unsigned layout=0u;layout<3u;layout++) {
        perf_setup(layout);perf_collect("oracle",false);
        struct native_counts old=collect_native(0u),now=collect_native(1u);
        assert(!memcmp(&old.stats,&now.stats,sizeof(old.stats)));
        assert(now.commands<=old.commands && now.sectors<=old.sectors);
        printf("Native live FAT memo layout%u: same logical=%u chains=%u directory=%u; commands %llu -> %llu, sectors %llu -> %llu\n",
            layout,now.stats.reads,now.stats.chain_steps,now.stats.directory_sectors,
            (unsigned long long)old.commands,(unsigned long long)now.commands,
            (unsigned long long)old.sectors,(unsigned long long)now.sectors);
    }
    perf_setup(0u);perf_collect("alternating oracle",false);
    struct native_counts old=alternating(0u),now=alternating(1u);
    assert(now.commands<old.commands && now.sectors<old.sectors);
    printf("Alternating directory/FAT windows: same data, no metadata eviction; commands %llu -> %llu, sectors %llu -> %llu\n",
        (unsigned long long)old.commands,(unsigned long long)now.commands,
        (unsigned long long)old.sectors,(unsigned long long)now.sectors);
    puts("PASS native memo keeps complete ownership and parser results");return 0;
}
