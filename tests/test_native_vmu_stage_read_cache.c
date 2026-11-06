/* SPDX-License-Identifier: GPL-3.0-only */
/* Compare the previous FAT-only adapter with both independent validation
 * snapshots, using the production filesystem/session/checkpoint/seal code. */
#if defined(KUI_STAGE_CACHE_FAT32)
#define main native_cache_unused_fat32_suite
#include "test_vmu_persist_session_adversarial.c"
#undef main
#include "kui/vmu_persist_stage.h"
static struct kui_vmu_persist_stage_workspace native_cache_work;
static struct kui_vmu_durable_handoff native_cache_handoff;
#define PROFILE disk.profile
#define WORK native_cache_work
#define HANDOFF native_cache_handoff
#define CONTEXT (&disk)
#define READ raw_read
#define IDENTITY_FN raw_identity
#define WRITE card_write
#define SYNC card_sync
#define HEALTH raw_healthy
#define WRITES disk.writes
#define SYNCS disk.syncs
#define LABEL "FAT32"
#else
#define KUI_VMU_PERSIST_STAGE_MAIN native_cache_unused_exfat_suite
#include "test_vmu_persist_stage.c"
#define PROFILE card.expected
#define WORK card.work
#define HANDOFF card.handoff
#define CONTEXT (&card)
#define READ stage_read
#define IDENTITY_FN stage_identity
#define WRITE stage_write
#define SYNC stage_sync
#define HEALTH stage_health
#define WRITES card.writes
#define SYNCS card.syncs
#define LABEL "exFAT"
#endif
#include "kui/smash_fat_read_cache.h"
#include "kui/vc2_ce_vmu_stage_cache.h"
static struct kui_smash_fat_read_cache fat_cache;
static struct kui_vc2_ce_vmu_stage_cache read_cache;
static struct kui_retail_manifest game;
static unsigned optimized,commands,sectors,requests,largest_run,validation_runs;
static unsigned fail_validation_run,damage_checkpoint,force_unhealthy;
static void invalidate(void) {
    kui_smash_fat_cache_begin_epoch(&fat_cache);
    kui_vc2_ce_vmu_stage_cache_invalidate(&read_cache);
}
static bool adapter_health(void *context) {return !force_unhealthy && HEALTH(context);}
static int adapter_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    invalidate();return IDENTITY_FN(context,out);
}
static int adapter_run(void *context,uint32_t first,uint32_t count,void *out) {
    assert(count && count<=KUI_VC2_STAGE_READ_BLOCKS && !((uintptr_t)out&31u));
    ++commands;sectors+=count;if(count>largest_run)largest_run=count;
    if(WORK.session.phase==KUI_VMU_PERSIST_SESSION_VALIDATION && count>1u &&
       ++validation_runs==fail_validation_run)return -1;
    for(uint32_t i=0u;i<count;i++)if(READ(context,(uint64_t)first+i,(uint8_t *)out+i*512u))return -1;
    return 0;
}
static int adapter_one(void *context,uint32_t lba,uint8_t out[512]) {
    if(optimized)return kui_vc2_ce_vmu_stage_cache_metadata_read(&read_cache,&PROFILE,
        WORK.session.phase==KUI_VMU_PERSIST_SESSION_ADMISSION,lba,out,adapter_run,context);
    ++commands;++sectors;return READ(context,lba,out);
}
static int adapter_window(void *context,uint32_t lba,uint32_t count,uint8_t *out,uint32_t *verified) {
    *verified=0u;++commands;sectors+=count;
    for(uint32_t i=0u;i<count;i++) {
        if(READ(context,(uint64_t)lba+i,out+i*512u))return *verified?0:-1;
        ++*verified;
    }
    if(optimized && WORK.session.phase==KUI_VMU_PERSIST_SESSION_ADMISSION)
        assert(!kui_vc2_ce_vmu_stage_cache_admission_store(&read_cache,&PROFILE,lba,*verified,out));
    return 0;
}
static int adapter_read(void *context,uint64_t lba,void *out) {
    ++requests;if(lba>UINT32_MAX)return -1;
    if(optimized && WORK.session.phase==KUI_VMU_PERSIST_SESSION_ADMISSION) {
        int hit=kui_vc2_ce_vmu_stage_cache_admission_peek(&read_cache,&PROFILE,lba,out);
        if(hit)return hit>0?0:-1;
    }
    if(optimized && WORK.session.phase==KUI_VMU_PERSIST_SESSION_VALIDATION)
        return kui_vc2_ce_vmu_stage_cache_read(&read_cache,&PROFILE,false,true,lba,out,adapter_run,context);
    const struct kui_smash_fat_read_ops ops={context,adapter_one,adapter_window};
    return kui_smash_fat_cache_read(&fat_cache,&ops,(uint32_t)lba,out);
}
static int adapter_write(void *context,uint64_t lba,const void *in) {
    invalidate();return WRITE(context,lba,in);
}
static int adapter_sync(void *context) {
    invalidate();int result=SYNC(context);
    if(!result && damage_checkpoint && WORK.phase==KUI_VMU_PERSIST_STAGE_CHECKPOINT && WRITES) {
#if defined(KUI_STAGE_CACHE_FAT32)
        disk.cache[255][0]=disk.durable[255][0]=0u;
#else
        card.base[255][0]=0u;
#endif
    }
    return result;
}
static void adapter_fresh(unsigned enabled,unsigned imported,unsigned recovery) {
#if defined(KUI_STAGE_CACHE_FAT32)
    assert(!imported);fresh();
    if(recovery)seed_pending(0u,0u,1u);
    memset(&WORK,0,sizeof(WORK));memset(&HANDOFF,0,sizeof(HANDOFF));
#else
    if(imported)stage_imported_pair_fresh();else stage_fresh();
    if(recovery)stage_seed_real_marker();
#endif
    memset(&fat_cache,0,sizeof(fat_cache));memset(&read_cache,0,sizeof(read_cache));
    assert(kui_smash_fat_cache_configure(&fat_cache,PROFILE.card_sectors,PROFILE.partition_first,PROFILE.partition_end));
    optimized=enabled;commands=sectors=requests=largest_run=validation_runs=0u;
    fail_validation_run=damage_checkpoint=force_unhealthy=0u;
    memset(&game,0,sizeof(game));game.card_sectors=PROFILE.card_sectors;
    game.partition_start=(uint32_t)PROFILE.partition_first;game.partition_end=(uint32_t)PROFILE.partition_end;
    game.storage_transport=KUI_STORAGE_SCI;game.reader=KUI_RETAIL_READER_STANDARD;
    game.track_count=game.extent_count=1u;game.session_lba=game.boot_lba=45000u;game.boot_bytes=2048u;
    memcpy(game.title,"Cache fixture",14u);memcpy(game.bootfile,"1ST_READ.BIN",13u);
    game.slots[0].track=(struct kui_retail_track){45000u,45004u,1u,1u,4u};
#if defined(KUI_STAGE_CACHE_FAT32)
    game.slots[1].extent=(struct kui_retail_extent){0u,10000u,19u};
#else
    game.slots[1].extent=(struct kui_retail_extent){0u,1000u,19u};
#endif
    assert(!kui_vmu_persist_stage_admit(&PROFILE,&game));
}
static int adapter_prepare(void) {
    const struct kui_vmu_persist_session_ops ops={CONTEXT,adapter_identity,adapter_read,
        adapter_write,adapter_sync,adapter_health};
    return kui_vmu_persist_stage_prepare(&PROFILE,&game,&ops,&WORK,&HANDOFF);
}
static void require_zero_handoff(void) {
    const uint8_t *p=(const void *)&HANDOFF;
    for(size_t i=0u;i<sizeof(HANDOFF);i++)assert(!p[i]);
}
static void equivalent(unsigned imported,unsigned recovery) {
    adapter_fresh(0u,imported,recovery);assert(!adapter_prepare());
    struct kui_vmu_durable_handoff expected=HANDOFF;
    unsigned old_commands=commands,old_sectors=sectors,old_requests=requests;
    unsigned old_writes=WRITES,old_syncs=SYNCS;
    adapter_fresh(1u,imported,recovery);assert(!adapter_prepare());
    assert(!memcmp(&expected,&HANDOFF,sizeof(expected)));
    assert(WRITES==old_writes && SYNCS==old_syncs && requests==old_requests);
    assert(read_cache.base_loads==2u && validation_runs==16u && largest_run==32u);
    assert(!read_cache.base_valid && commands<old_commands && sectors<old_sectors);
    assert(!kui_vmu_durable_handoff_validate(&HANDOFF));
    printf("Native %s imported=%u redo=%u: same %u logical reads, SD commands %u -> %u, sectors %u -> %u; both image validations retained\n",
        LABEL,imported,recovery,requests,old_commands,commands,old_sectors,sectors);
}
static void failures(void) {
    for(unsigned at=1u;at<=16u;at++) {
        adapter_fresh(1u,0u,0u);fail_validation_run=at;
        assert(adapter_prepare());require_zero_handoff();assert(WORK.session.faulted);
    }
    adapter_fresh(1u,0u,0u);damage_checkpoint=1u;
    assert(adapter_prepare());require_zero_handoff();assert(read_cache.base_loads==2u);
    adapter_fresh(1u,0u,1u);
#if defined(KUI_STAGE_CACHE_FAT32)
    disk.fat[JOURNAL]=0u;
#else
    allocated(60u,0);
#endif
    assert(adapter_prepare());require_zero_handoff();assert(!WRITES && !SYNCS);
    adapter_fresh(1u,0u,1u);
#if defined(KUI_STAGE_CACHE_FAT32)
    disk.actual_identity.cid[0]^=1u;
#else
    card.actual.cid[0]^=1u;
#endif
    assert(adapter_prepare());require_zero_handoff();assert(!WRITES && !SYNCS);
    adapter_fresh(1u,0u,0u);force_unhealthy=1u;
    assert(adapter_prepare());require_zero_handoff();assert(!WRITES && !SYNCS);
}
int main(void) {
    _Static_assert(sizeof(struct kui_vc2_ce_vmu_stage_cache)==165472u,"Accepted72 temporary cache size");
    for(unsigned recovery=0u;recovery<2u;recovery++)equivalent(0u,recovery);
#if !defined(KUI_STAGE_CACHE_FAT32)
    for(unsigned recovery=0u;recovery<2u;recovery++)equivalent(1u,recovery);
#endif
    failures();puts("PASS shared snapshots: ownership, both validators, checkpoint mutation, identity, health, failed snapshots");
    return 0;
}
