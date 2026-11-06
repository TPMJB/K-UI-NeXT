/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual filesystem admission through its old single-sector callback and the
 * new transaction-local cache, using the independent sparse FAT32 fixture. */
#include "kui/smash_fat_read_cache.h"
#define main original_fat_admission_fixture_main
#include "test_vmu_fs_admission_adversarial.c"
#undef main

static struct {
    struct kui_smash_fat_read_cache cache;
    struct kui_vmu_fs_workspace work;
    struct kui_vmu_fs_input input;
    uint32_t partition,copies,one_fail,window_fail;
    unsigned fault,mirror_bad,bad_vbr,cached;
    unsigned ones,windows,blocks,requests,answers,max_run,simulated_writes;
    uint64_t trace;
} model;

static uint32_t volume(void) {return VOLUME_SECTORS+(model.copies-1u)*FAT_SECTORS;}
static uint32_t first_fat(unsigned copy) {return model.partition+RESERVED+copy*FAT_SECTORS;}
static uint32_t end_volume(void) {return model.partition+volume();}
static void trace_bytes(const void *data,size_t count) {
    const uint8_t *p=data;
    while(count--) {model.trace^=*p++;model.trace*=UINT64_C(1099511628211);}
}
static int answer(uint32_t lba,uint8_t out[512]) {
    if(!lba) {
        assert(!read_sector(&disk,0u,out));
        put32(out+454u,model.partition);put32(out+458u,volume());return 0;
    }
    if(lba<model.partition || lba>=end_volume())return -1;
    uint32_t relative=lba-model.partition,original=PARTITION+relative;
    bool mirror=relative>=RESERVED+FAT_SECTORS && relative<RESERVED+2u*FAT_SECTORS && model.copies==2u;
    if(mirror)original-=FAT_SECTORS;
    else if(relative>=RESERVED+model.copies*FAT_SECTORS)original-=(model.copies-1u)*FAT_SECTORS;
    assert(!read_sector(&disk,original,out));
    if(relative==0u || relative==6u) {
        out[16]=(uint8_t)model.copies;put32(out+28u,model.partition);put32(out+32u,volume());
        if(model.bad_vbr)out[12]=0u;
    }
    if(mirror && model.mirror_bad && relative==RESERVED+FAT_SECTORS)out[17]^=0x80u;
    return 0;
}
static int one(void *context,uint32_t lba,uint8_t out[512]) {
    assert(context==&model);++model.ones;
    if(lba==model.one_fail)return -1;
    return answer(lba,out);
}
static int window(void *context,uint32_t lba,uint32_t count,uint8_t *out,uint32_t *verified) {
    assert(context==&model && out && verified && count && count<=16u);
    bool bounded=false;
    for(unsigned copy=0;copy<model.copies;copy++)
        if(lba>=first_fat(copy) && lba-first_fat(copy)<FAT_SECTORS &&
           count<=FAT_SECTORS-(lba-first_fat(copy)))bounded=true;
    assert(bounded && (uint64_t)lba+count<=end_volume());
    ++model.windows;model.blocks+=count;if(count>model.max_run)model.max_run=count;
    memset(out,0xd7,(size_t)count*512u);*verified=0u;
    for(uint32_t index=0;index<count;index++) {
        if(model.fault==1u && lba+index==model.window_fail)return index?0:-1;
        assert(!answer(lba+index,out+index*512u));++*verified;
    }
    if(model.fault==2u)return -1; /* Valid bytes, uncertain stop: publish none. */
    if(model.fault==3u)*verified=0u;
    if(model.fault==4u)*verified=count+1u;
    return 0;
}
static const struct kui_smash_fat_read_ops ops={&model,one,window};
static int requested(void *context,uint32_t lba,uint8_t out[512]) {
    assert(context==&model);++model.requests;
    uint8_t before[512],expected[512];memcpy(before,out,512u);
    int result=model.cached?kui_smash_fat_cache_read(&model.cache,&ops,lba,out):one(&model,lba,out);
    trace_bytes(&lba,sizeof(lba));trace_bytes(&result,sizeof(result));
    if(!result) {
        assert(!answer(lba,expected) && !memcmp(out,expected,512u));
        trace_bytes(out,512u);++model.answers;
    } else assert(!memcmp(out,before,512u));
    return result;
}
static void counters(bool cached) {
    model.cached=cached;model.ones=model.windows=model.blocks=model.requests=model.answers=model.max_run=0u;
    model.trace=UINT64_C(1469598103934665603);
}
static void setup(unsigned copies,uint32_t partition) {
    fresh();memset(&model,0,sizeof(model));model.copies=copies;model.partition=partition;
    model.one_fail=model.window_fail=UINT32_MAX;
    model.input=disk.input;model.input.context=&model;model.input.read_sector=requested;
    model.input.partition_first=partition;model.input.partition_end=end_volume();model.input.card_sectors=end_volume();
    assert(kui_smash_fat_cache_configure(&model.cache,end_volume(),partition,end_volume()));
    assert(sizeof(model.cache)<17000u && !((uintptr_t)model.cache.windows[0].data&31u) &&
           !((uintptr_t)model.cache.windows[1].data&31u));
}
static int collect_pair(struct kui_vmu_persist_profile *saved) {
    struct kui_vmu_persist_profile old,next;
    counters(false);int previous=kui_vmu_fs_collect(&model.input,&old,&model.work);
    unsigned requests=model.requests,physical=model.ones;uint64_t trace=model.trace;
    counters(true);kui_smash_fat_cache_begin_epoch(&model.cache);
    int result=kui_vmu_fs_collect(&model.input,&next,&model.work);
    assert(result==previous && !memcmp(&old,&next,sizeof(old)) && requests==model.requests && trace==model.trace);
    assert(!model.simulated_writes && model.max_run<=16u);
    if(!result) {
        assert(model.windows && model.windows+model.ones<physical/2u);
        if(saved)*saved=next;
        printf("FAT%u collect: %u requested sectors, %u old sessions -> %u single + %u bounded windows; exact answers/maps\n",
            model.copies,requests,physical,model.ones,model.windows);
    }
    return result;
}
static int admit_pair(const struct kui_vmu_persist_profile *expected) {
    struct kui_vmu_persist_profile before=*expected;
    counters(false);int previous=kui_vmu_fs_admit(&model.input,expected,&model.work);
    unsigned requests=model.requests;uint64_t trace=model.trace;
    counters(true);kui_smash_fat_cache_begin_epoch(&model.cache);
    int result=kui_vmu_fs_admit(&model.input,expected,&model.work);
    assert(result==previous && requests==model.requests && trace==model.trace && !model.simulated_writes);
    assert(!memcmp(expected,&before,sizeof(before)));return result;
}
static int direct(uint32_t lba) {
    uint8_t out[512];memset(out,0xa5,sizeof(out));
    return requested(&model,lba,out);
}
static void successful_fresh_admission(void) {
    for(unsigned copies=1u;copies<=2u;copies++) {
        setup(copies,PARTITION);struct kui_vmu_persist_profile expected;
        assert(!collect_pair(&expected) && !admit_pair(&expected));
        assert(expected.geometry.fat_count==copies && expected.geometry.fat_first_lba==first_fat(0u));
    }
}
static void fresh_boot_only_and_unknown_geometry(void) {
    setup(1u,PARTITION);counters(true);
    assert(!direct(first_fat(0u)) && !direct(first_fat(0u)) && model.ones==2u && !model.windows);
    assert(!direct(model.partition));unsigned singles=model.ones;
    assert(!direct(first_fat(0u)) && !direct(first_fat(0u)+1u));
    assert(model.ones==singles && model.windows==1u);
    kui_smash_fat_cache_begin_epoch(&model.cache);model.one_fail=model.partition;
    assert(direct(model.partition) && !model.cache.fat_count);model.one_fail=UINT32_MAX;
    /* A failed fresh read may leave an older valid VBR in the caller's
     * destination. Those retained bytes cannot teach fresh geometry. */
    uint8_t old_boot[512];assert(!answer(model.partition,old_boot));
    model.one_fail=model.partition;
    assert(kui_smash_fat_cache_read(&model.cache,&ops,model.partition,old_boot) && !model.cache.fat_count);
    model.one_fail=UINT32_MAX;
    unsigned windows=model.windows;assert(!direct(first_fat(0u)) && model.windows==windows);
    model.bad_vbr=1u;assert(!direct(model.partition) && !model.cache.fat_count);
    assert(!direct(first_fat(0u)) && model.windows==windows);
}
static void independent_copies_and_bounded_tail(void) {
    setup(2u,PARTITION);counters(true);model.mirror_bad=1u;
    assert(!direct(model.partition));
    assert(!direct(first_fat(0u)) && !direct(first_fat(1u)) && model.windows==2u);
    assert(!direct(first_fat(0u)+1u) && !direct(first_fat(1u)+1u) && model.windows==2u);
    assert(!direct(first_fat(0u)+FAT_SECTORS-1u) && model.cache.windows[0].count==1u);
    assert(!direct(first_fat(1u)+FAT_SECTORS-1u) && model.cache.windows[1].count==1u);
    unsigned windows=model.windows,singles=model.ones;
    assert(!direct(first_fat(1u)+FAT_SECTORS) && model.windows==windows && model.ones==singles+1u);
}
static void prefix_crc_and_teardown_failures(void) {
    for(unsigned fault=1u;fault<=4u;fault++) {
        setup(1u,PARTITION);counters(true);assert(!direct(model.partition));
        model.fault=fault;model.window_fail=first_fat(0u);
        assert(direct(first_fat(0u)) && model.windows==1u);
        model.fault=0u;assert(!direct(first_fat(0u)) && model.windows==2u);
        assert(!direct(first_fat(0u)+1u) && model.windows==2u);
    }
    setup(1u,PARTITION);counters(true);assert(!direct(model.partition));
    model.fault=1u;model.window_fail=first_fat(0u)+3u;
    assert(!direct(first_fat(0u)) && model.cache.windows[0].count==3u);
    assert(!direct(first_fat(0u)+1u) && !direct(first_fat(0u)+2u) && model.windows==1u);
    assert(direct(first_fat(0u)+3u) && model.windows==2u); /* Poisoned suffix is never a hit. */
    model.fault=0u;assert(!direct(first_fat(0u)+3u) && model.windows==3u);
}
static void mutations_stay_fresh_before_write_authority(void) {
    struct kui_vmu_persist_profile expected;
    setup(2u,PARTITION);assert(!collect_pair(&expected));
    disk.fat[100u]=BASE_FIRST; /* Fresh global incoming-edge scan must refuse. */
    assert(admit_pair(&expected)==KUI_VMU_FS_CROSSLINK);
    setup(2u,PARTITION);assert(!collect_pair(&expected));model.mirror_bad=1u;
    assert(admit_pair(&expected)==KUI_VMU_FS_CORRUPT);
    setup(1u,PARTITION);assert(!collect_pair(&expected));
    /* A new card/partition configuration cannot retain the old FAT address. */
    model.partition=4096u;model.copies=2u;
    model.input.partition_first=model.partition;model.input.partition_end=end_volume();model.input.card_sectors=end_volume();
    assert(kui_smash_fat_cache_configure(&model.cache,end_volume(),model.partition,end_volume()));
    counters(true);assert(direct(PARTITION+RESERVED) && !model.windows);
    assert(!collect_pair(&expected) && !admit_pair(&expected));
}
static void write_epoch_invalidates_geometry_and_data(void) {
    setup(1u,PARTITION);counters(true);assert(!direct(model.partition) && !direct(first_fat(0u)));
    disk.fat[100u]=EOC;++model.simulated_writes; /* Stage integration must call this boundary. */
    kui_smash_fat_cache_begin_epoch(&model.cache);
    assert(!model.cache.fat_count && !model.cache.windows[0].count && !model.cache.windows[1].count);
    unsigned windows=model.windows,singles=model.ones;
    assert(!direct(first_fat(0u)) && model.windows==windows && model.ones==singles+1u);
    assert(!direct(model.partition) && !direct(first_fat(0u)) && model.windows==windows+1u);
}
static void arguments_refuse_before_io(void) {
    setup(1u,PARTITION);counters(true);uint8_t out[512];memset(out,0xa5,sizeof(out));
    assert(!kui_smash_fat_cache_configure(NULL,1u,0u,1u));
    assert(!kui_smash_fat_cache_configure(&model.cache,0u,0u,1u));
    assert(!kui_smash_fat_cache_configure(&model.cache,UINT64_C(0x100000001),0u,1u));
    assert(!kui_smash_fat_cache_configure(&model.cache,100u,99u,101u));
    assert(kui_smash_fat_cache_read(NULL,&ops,0u,out));
    assert(kui_smash_fat_cache_read(&model.cache,NULL,0u,out));
    assert(kui_smash_fat_cache_read(&model.cache,&ops,0u,NULL));
    assert(kui_smash_fat_cache_read(&model.cache,&ops,0u,model.cache.windows[0].data));
    assert(kui_smash_fat_cache_read(&model.cache,&ops,end_volume(),out));
    assert(!model.ones && !model.windows);
}
int main(void) {
    successful_fresh_admission();fresh_boot_only_and_unknown_geometry();
    independent_copies_and_bounded_tail();prefix_crc_and_teardown_failures();
    mutations_stay_fresh_before_write_authority();write_epoch_invalidates_geometry_and_data();
    arguments_refuse_before_io();
    puts("Fresh FAT cache: actual old/new admission/maps/requested bytes match; crosslinks, mirror mutation, CRC prefixes, failed stops, epochs and write invalidation passed");
    return 0;
}
