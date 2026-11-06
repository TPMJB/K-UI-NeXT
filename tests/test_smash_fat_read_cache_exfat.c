/* SPDX-License-Identifier: GPL-3.0-only */
/* Retain full exFAT admission semantics when the generic fresh reader learns
 * its one bounded FAT from the current main VBR. */
#include "kui/smash_fat_read_cache.h"
#define main original_exfat_admission_fixture_main
#include "test_vmu_fs_admission.c"
#undef main

static struct kui_smash_fat_read_cache cache;
static struct kui_vmu_fs_workspace cached_work;
static unsigned cached,ones,windows,requests,fault;
static uint32_t bad_lba;
static uint64_t trace;
static void note(const void *data,size_t bytes) {
    const uint8_t *p=data;
    while(bytes--) {trace^=*p++;trace*=UINT64_C(1099511628211);}
}
static int single(void *context,uint32_t lba,uint8_t out[512]) {
    assert(context==&cache);++ones;
    if(fault && lba==bad_lba)return -1;
    return read_sector(&disk,lba,out);
}
static int run(void *context,uint32_t lba,uint32_t count,uint8_t *out,uint32_t *verified) {
    assert(context==&cache && count && count<=16u && lba>=FS_FAT && lba<FS_FAT+FS_FAT_SECTORS);
    assert(count<=FS_FAT+FS_FAT_SECTORS-lba);++windows;*verified=0u;
    memset(out,0xc7,(size_t)count*512u);
    for(uint32_t i=0;i<count;i++) {
        if(fault && lba+i==bad_lba)return i?0:-1;
        assert(!read_sector(&disk,lba+i,out+i*512u));++*verified;
    }
    return 0;
}
static const struct kui_smash_fat_read_ops io={&cache,single,run};
static int requested_sector(void *context,uint32_t lba,uint8_t out[512]) {
    assert(context==&cache);++requests;uint8_t before[512],oracle[512];memcpy(before,out,512u);
    int result=cached?kui_smash_fat_cache_read(&cache,&io,lba,out):single(&cache,lba,out);
    note(&lba,sizeof(lba));note(&result,sizeof(result));
    if(!result) {assert(!read_sector(&disk,lba,oracle) && !memcmp(out,oracle,512u));note(out,512u);}
    else assert(!memcmp(out,before,512u));
    return result;
}
static void reset_counts(bool enabled) {
    cached=enabled;ones=windows=requests=0u;trace=UINT64_C(1469598103934665603);
}
static int compare_collect(void) {
    struct kui_vmu_fs_input input=disk.input;
    struct kui_vmu_persist_profile old,next;
    input.context=&cache;input.read_sector=requested_sector;
    assert(kui_smash_fat_cache_configure(&cache,input.card_sectors,input.partition_first,input.partition_end));
    reset_counts(false);int baseline=kui_vmu_fs_collect(&input,&old,&disk.work);
    uint64_t previous_trace=trace;unsigned previous_requests=requests;
    reset_counts(true);kui_smash_fat_cache_begin_epoch(&cache);
    int result=kui_vmu_fs_collect(&input,&next,&cached_work);
    assert(result==baseline && !memcmp(&old,&next,sizeof(old)) && trace==previous_trace && requests==previous_requests);
    if(!result) {
        assert(windows && cache.fat_count==1u && cache.fat_blocks==FS_FAT_SECTORS);
        struct kui_vmu_persist_profile expected=next,before=next;
        kui_smash_fat_cache_begin_epoch(&cache);
        assert(!kui_vmu_fs_admit(&input,&expected,&cached_work) && !memcmp(&expected,&before,sizeof(expected)));
    }
    return result;
}
int main(void) {
    fresh();fault=0u;assert(!compare_collect());
    /* A failed unrequested third FAT sector cannot reject the first two
     * verified sectors used by this actual contiguous-file admission. */
    fresh();fault=1u;bad_lba=FS_FAT+2u;assert(!compare_collect());
    fresh();fault=1u;bad_lba=FS_FAT;assert(compare_collect()==KUI_VMU_FS_IO);
    fresh();fault=0u;
    file_entry(disk.dirs[0]+160u,"alias",32u,21u,16u,1u);
    assert(compare_collect()==KUI_VMU_FS_CROSSLINK);
    puts("Fresh exFAT cache: exact old/new collect/admit answers and ownership; unrequested CRC suffix accepted, failed requested FAT/crosslink refused");
    return 0;
}
