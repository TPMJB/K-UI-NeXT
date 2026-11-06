/* SPDX-License-Identifier: GPL-3.0-only */
#define KUI_VMU_PERSIST_STAGE_MAIN vc2_cache_fixture_unused_main
#include "test_vmu_persist_stage.c"
#include "kui/vc2_ce_vmu_stage_cache.h"

static struct kui_vc2_ce_vmu_stage_cache cache;
static unsigned fail_run,run_calls,largest_run;
static int cached_raw(void *context,uint32_t sector,uint32_t count,void *out) {
    assert(context==&card && count && count<=KUI_VC2_STAGE_READ_BLOCKS);
    assert(!((uintptr_t)out&31u));
    ++run_calls;if(count>largest_run)largest_run=count;
    if(run_calls==fail_run)return -1;
    for(uint32_t i=0u;i<count;i++)if(stage_read(context,(uint64_t)sector+i,(uint8_t *)out+512u*i))return -1;
    return 0;
}
static int cached_read(void *context,uint64_t sector,void *out) {
    uint32_t phase=card.work.session.phase;
    return kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,
        phase==KUI_VMU_PERSIST_SESSION_ADMISSION,phase==KUI_VMU_PERSIST_SESSION_VALIDATION,
        sector,out,cached_raw,context);
}
static int cached_write(void *context,uint64_t sector,const void *in) {
    kui_vc2_ce_vmu_stage_cache_invalidate(&cache);return stage_write(context,sector,in);
}
static int cached_sync(void *context) {
    kui_vc2_ce_vmu_stage_cache_invalidate(&cache);return stage_sync(context);
}
static void cache_fresh(void) {
    stage_fresh();memset(&cache,0,sizeof(cache));fail_run=run_calls=largest_run=0u;
}
static int cached_prepare(void) {
    const struct kui_vmu_persist_session_ops ops={&card,stage_identity,cached_read,cached_write,cached_sync,stage_health};
    return kui_vmu_persist_stage_prepare(&card.expected,&card.game,&ops,&card.work,&card.handoff);
}
static void equivalent(unsigned imported,unsigned recovery) {
    struct kui_vmu_durable_handoff expected;
    if(imported)stage_imported_pair_fresh();else stage_fresh();
    if(recovery)stage_seed_real_marker();else card.base[123][7]^=0x71u;
    assert(!stage_prepare());expected=card.handoff;unsigned baseline=card.reads;
    unsigned expected_writes=card.writes,expected_syncs=card.syncs,expected_identity=card.identities;
    if(imported)stage_imported_pair_fresh();else stage_fresh();
    memset(&cache,0,sizeof(cache));fail_run=run_calls=largest_run=0u;
    if(recovery)stage_seed_real_marker();else card.base[123][7]^=0x71u;
    assert(!cached_prepare());assert(!memcmp(&expected,&card.handoff,sizeof(expected)));
    assert(card.writes==expected_writes && card.syncs==expected_syncs && card.identities==expected_identity);
    assert(cache.base_loads==1u && cache.hits>=525u && !cache.base_valid);
    assert(largest_run==32u && card.reads<baseline && cache.commands<cache.requests);
    printf("imported=%u recovery=%u identical handoff: raw sectors %u -> %u, commands %u, requests %u, hits %u\n",
        imported,recovery,baseline,card.reads,cache.commands,cache.requests,cache.hits);
}
static void cache_boundaries(void) {
    uint8_t out[512],before[512];cache_fresh();memset(out,0xa5,sizeof(out));memcpy(before,out,sizeof(out));
    /* Exactly bounded at the final partial FAT window and each copy. */
    const struct kui_vmu_persist_geometry *g=&card.expected.geometry;
    uint64_t end=g->fat_first_lba+g->fat_sectors;
    assert(!kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,true,false,end-1u,out,cached_raw,&card));
    assert(cache.sectors==((g->fat_sectors-1u)%KUI_VC2_STAGE_FAT_BLOCKS)+1u);
    assert(!kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,true,false,end-1u,out,cached_raw,&card));
    assert(cache.hits==1u);
    /* Reads outside actual capacity never call the backend or change output. */
    memcpy(out,before,sizeof(out));unsigned calls=run_calls;
    assert(kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,true,true,card.expected.card_sectors,out,cached_raw,&card));
    assert(calls==run_calls && !memcmp(out,before,sizeof(out)));
    /* Every write/sync invalidates all windows before the backend can fail. */
    cache.base_valid=1u;cache.fat_blocks[0]=cache.fat_blocks[1]=KUI_VC2_STAGE_FAT_BLOCKS;
    card.fail_write=1u;assert(cached_write(&card,lba(60u),before));
    assert(!cache.base_valid && !cache.fat_blocks[0] && !cache.fat_blocks[1]);
    cache.base_valid=1u;cache.fat_blocks[0]=KUI_VC2_STAGE_FAT_BLOCKS;card.fail_sync=1u;assert(cached_sync(&card));
    assert(!cache.base_valid && !cache.fat_blocks[0]);
}
static void fragmented_snapshot(void) {
    cache_fresh();
    /* Two useful extents: the second begins at a distinct real fixture range.
     * Never prefetch across the gap or consume its unrelated sectors. */
    struct kui_vmu_persist_file *f=&card.expected.files[KUI_VMU_PERSIST_BASE];
    f->useful_count=2u;f->useful[0].blocks=31u;
    f->useful[1]=(struct kui_vmu_persist_extent){31u,(uint32_t)lba(20u)+64u,225u};
    uint8_t out[512];
    assert(!kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,false,true,lba(20u),out,cached_raw,&card));
    assert(cache.base_loads==1u && cache.sectors==256u && run_calls==9u);
    assert(!memcmp(out,card.base[0],512u));
    assert(!memcmp(cache.base[31],card.base[64],512u));
}
static int fat_raw(void *context,uint32_t sector,uint32_t count,void *out) {
    const struct kui_vmu_persist_geometry *g=context;
    assert(count==KUI_VC2_STAGE_FAT_BLOCKS && sector>=g->fat_first_lba &&
           (uint64_t)sector+count<=g->fat_first_lba+g->fat_count*g->fat_sectors);
    assert((sector-g->fat_first_lba)%g->fat_sectors+count<=g->fat_sectors);
    for(uint32_t i=0u;i<count;i++) {
        memset((uint8_t *)out+512u*i,0x6d,512u);
        memcpy((uint8_t *)out+512u*i,&sector,sizeof(sector));++sector;
    }
    return 0;
}
static void full_mirrored_fat(void) {
    cache_fresh();struct kui_vmu_persist_geometry *g=&card.expected.geometry;
    g->fat_first_lba=32u;g->fat_sectors=32768u;g->fat_count=2u;
    card.expected.card_sectors=131072u;
    uint8_t out[512];
    for(uint32_t sector=0u;sector<32768u;sector++)for(unsigned copy=0u;copy<2u;copy++) {
        uint32_t physical=(uint32_t)g->fat_first_lba+copy*(uint32_t)g->fat_sectors+sector,value;
        assert(!kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,true,false,physical,out,fat_raw,g));
        memcpy(&value,out,sizeof(value));assert(value==physical);
        for(unsigned i=4u;i<512u;i++)assert(out[i]==0x6d);
    }
    assert(cache.requests==65536u && cache.sectors==65536u && cache.commands==2048u && cache.hits==63488u);
    puts("Maximum mirrored FAT: all 65536 sectors preserved; SD commands 65536 -> 2048");
}
static unsigned memo_raw_calls,memo_partial;
static int memo_raw(void *context,uint32_t lba,uint32_t count,void *out) {
    (void)context;assert(count && count<=32u && !((uintptr_t)out&31u));++memo_raw_calls;
    for(uint32_t i=0u;i<count;i++) {
        memset((uint8_t *)out+i*512u,(int)((lba+i)&255u),512u);
        if(memo_partial && i==count/2u)return -1;
    }
    return 0;
}
static void admission_memo(void) {
    cache_fresh();memo_raw_calls=memo_partial=0u;
    card.expected.partition_first=500u;card.expected.partition_end=6000u;
    uint8_t out[512];
    for(unsigned i=0u;i<3u;i++)
        assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,0u,out,memo_raw,NULL));
    assert(memo_raw_calls==1u && cache.hits==2u);
    /* Preserve the accepted direct metadata mapping and full 256 lines.
     * Colliding exact tags miss safely rather than aliasing stale bytes. */
    assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,257u,out,memo_raw,NULL));
    for(unsigned i=0u;i<512u;i++)assert(out[i]==1u);
    assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,0u,out,memo_raw,NULL));
    for(unsigned i=0u;i<512u;i++)assert(!out[i]);
    assert(memo_raw_calls==3u);
    /* A metadata-only caller never prefetches profile-derived FAT windows. */
    uint32_t fat=(uint32_t)card.expected.geometry.fat_first_lba;
    unsigned sectors=cache.sectors;
    assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,fat,out,memo_raw,NULL));
    assert(cache.sectors==sectors+1u);
    /* A mode transition drops old metadata and a failed run cannot mutate
     * caller output or publish a partly filled FAT window. */
    uint8_t before[512];memset(out,0xa5,sizeof(out));memcpy(before,out,sizeof(out));
    memo_partial=1u;
    assert(kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,true,false,fat,out,memo_raw,NULL));
    assert(!memcmp(out,before,sizeof(out)) && !cache.fat_blocks[0]);
    memo_partial=0u;
    assert(!kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,true,false,fat,out,memo_raw,NULL));
    sectors=cache.commands;
    assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,fat,out,memo_raw,NULL));
    assert(cache.commands==sectors+1u);
}
static void live_boot_and_phase_epochs(void) {
    cache_fresh();uint8_t out[512],before[512];memset(out,0xa5,sizeof(out));memcpy(before,out,sizeof(out));
    uint64_t first=card.expected.partition_first;
    assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,first,out,cached_raw,&card));
    assert(cache.commands==2u && cache.sectors==24u); /* fresh VBR, then23 */
    assert(!memcmp(out,disk.boot[0],512u));
    for(unsigned i=1u;i<24u;i++) {
        assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,first+i,out,cached_raw,&card));
        assert(!memcmp(out,disk.boot[i],512u));
    }
    assert(cache.commands==2u);
    /* Snapshot storage was previously memo data. It must be freshly filled. */
    assert(!kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,false,true,lba(20u),out,cached_raw,&card));
    assert(cache.base_loads==1u && !memcmp(out,card.base[0],512u));
    card.base[0][31]^=0x39u;
    assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,first,out,cached_raw,&card));
    assert(!cache.base_valid);
    assert(!kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,false,true,lba(20u),out,cached_raw,&card));
    assert(cache.base_loads==2u && !memcmp(out,card.base[0],512u));
    cache_fresh();disk.boot[0][3]='X';
    assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,first,out,cached_raw,&card));
    assert(cache.commands==1u && cache.sectors==1u); /* no stale-profile boot prefetch */
    cache_fresh();fail_run=2u;memcpy(out,before,sizeof(out));
    assert(kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,first,out,cached_raw,&card));
    assert(!memcmp(out,before,sizeof(out)));
}
static void verified_window_memo(void) {
    cache_fresh();uint8_t out[512],before[512];_Alignas(32) uint8_t input[3][512];
    memset(out,0xa5,sizeof(out));memcpy(before,out,sizeof(out));
    for(unsigned i=0u;i<3u;i++)memset(input[i],0x31+(int)i,512u);
    assert(!kui_vc2_ce_vmu_stage_cache_admission_peek(&cache,&card.expected,100u,out));
    assert(!memcmp(out,before,sizeof(out)));
    /* A caller cannot inject window data outside the admission epoch. */
    assert(kui_vc2_ce_vmu_stage_cache_admission_store(&cache,&card.expected,100u,2u,input));
    assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,
        card.expected.partition_first,out,cached_raw,&card));
    assert(!kui_vc2_ce_vmu_stage_cache_admission_store(&cache,&card.expected,100u,2u,input));
    for(unsigned i=0u;i<2u;i++) {
        assert(kui_vc2_ce_vmu_stage_cache_admission_peek(&cache,&card.expected,100u+i,out)==1);
        assert(!memcmp(out,input[i],512u));
    }
    memcpy(out,before,sizeof(out));
    assert(!kui_vc2_ce_vmu_stage_cache_admission_peek(&cache,&card.expected,102u,out));
    assert(!memcmp(out,before,sizeof(out))); /* Unverified suffix never appears. */
    assert(kui_vc2_ce_vmu_stage_cache_admission_store(&cache,&card.expected,100u,0u,input));
    assert(kui_vc2_ce_vmu_stage_cache_admission_store(&cache,&card.expected,100u,33u,input));
    assert(kui_vc2_ce_vmu_stage_cache_admission_store(&cache,&card.expected,
        (uint32_t)card.expected.card_sectors-1u,2u,input));
    /* Four independently selected windows retain exact verified prefixes.
     * A fifth replaces only its bounded victim, never unrelated metadata. */
    for(unsigned i=1u;i<4u;i++)
        assert(!kui_vc2_ce_vmu_stage_cache_admission_store(&cache,&card.expected,100u+64u*i,1u,input[i%3u]));
    for(unsigned i=0u;i<4u;i++) {
        assert(kui_vc2_ce_vmu_stage_cache_admission_peek(&cache,&card.expected,100u+64u*i,out)==1);
        assert(!memcmp(out,input[i%3u],512u));
    }
    assert(!kui_vc2_ce_vmu_stage_cache_admission_store(&cache,&card.expected,356u,1u,input[1]));
    memcpy(out,before,sizeof(out));
    assert(!kui_vc2_ce_vmu_stage_cache_admission_peek(&cache,&card.expected,100u,out));
    assert(!memcmp(out,before,sizeof(out)));
    assert(kui_vc2_ce_vmu_stage_cache_admission_peek(&cache,&card.expected,356u,out)==1);
    assert(!memcmp(out,input[1],512u));
    /* The 23-sector boot receiver overlaps FAT memo bytes. Even a failed
     * boot fill must invalidate every overlapping FAT tag before writing. */
    uint32_t first=(uint32_t)card.expected.partition_first;
    assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,
        first^257u,out,memo_raw,NULL)); /* Evict only the VBR metadata tag. */
    fail_run=run_calls+2u;
    assert(kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,true,
        first,out,cached_raw,&card));
    for(unsigned i=0u;i<4u;i++)assert(!cache.memo_fat_blocks[i]);
    memcpy(out,before,sizeof(out));
    assert(!kui_vc2_ce_vmu_stage_cache_admission_peek(&cache,&card.expected,356u,out));
    assert(!memcmp(out,before,sizeof(out)));fail_run=0u;
    /* CE FAT admission keeps the original mapping, and the native-only APIs
     * cannot misinterpret those CE window bytes or tags. */
    assert(!kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,true,false,
        card.expected.geometry.fat_first_lba,out,cached_raw,&card));
    assert(kui_vc2_ce_vmu_stage_cache_admission_store(&cache,&card.expected,100u,2u,input));
    memcpy(out,before,sizeof(out));
    assert(!kui_vc2_ce_vmu_stage_cache_admission_peek(&cache,&card.expected,100u,out));
    assert(!memcmp(out,before,sizeof(out)));
    assert(!kui_vc2_ce_vmu_stage_cache_metadata_read(&cache,&card.expected,false,0u,out,cached_raw,&card));
    memcpy(out,before,sizeof(out));
    assert(!kui_vc2_ce_vmu_stage_cache_admission_peek(&cache,&card.expected,100u,out));
    assert(!memcmp(out,before,sizeof(out)));
    assert(!kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,false,true,lba(20u),out,cached_raw,&card));
    assert(kui_vc2_ce_vmu_stage_cache_admission_store(&cache,&card.expected,100u,2u,input));
    memcpy(out,before,sizeof(out));
    assert(!kui_vc2_ce_vmu_stage_cache_admission_peek(&cache,&card.expected,100u,out));
    assert(!memcmp(out,before,sizeof(out)));
    kui_vc2_ce_vmu_stage_cache_invalidate(&cache);
    assert(!cache.base_valid && !cache.mode);
    for(unsigned i=0u;i<sizeof(cache.memo_valid)/sizeof(cache.memo_valid[0]);i++)assert(!cache.memo_valid[i]);
}
static void failures(void) {
    for(unsigned at=1u;at<=8u;at++) {
        uint8_t out[512],before[512];cache_fresh();memset(out,0xa5,sizeof(out));memcpy(before,out,sizeof(out));
        fail_run=at;
        assert(kui_vc2_ce_vmu_stage_cache_read(&cache,&card.expected,false,true,lba(20u),out,cached_raw,&card));
        assert(!cache.base_valid && !memcmp(out,before,sizeof(out)));
    }
    cache_fresh();card.base[255][0]=0u;
    assert(cached_prepare()==KUI_VMU_PERSIST_STAGE_SESSION && !card.writes && !card.syncs);
    stage_handoff_zero();
    /* Changed ownership must still refuse before a valid redo can write. */
    cache_fresh();stage_seed_real_marker();
    file_entry(disk.dirs[3]+96u,"card.journal",32u,62u,1024u,3u);allocated(62u,1);
    assert(cached_prepare()==KUI_VMU_PERSIST_STAGE_SESSION && !card.writes && !card.syncs);
    assert(card.work.session.fs_result==KUI_VMU_FS_CHANGED);stage_handoff_zero();
    cache_fresh();stage_seed_real_marker();
    file_entry(disk.dirs[3]+96u,"card.journal",32u,122u,1024u,3u);allocated(122u,1);
    assert(cached_prepare()==KUI_VMU_PERSIST_STAGE_SESSION && !card.writes && !card.syncs);
    assert(card.work.session.fs_result==KUI_VMU_FS_PROTECTED);stage_handoff_zero();
    for(unsigned which=0u;which<7u;which++) {
        cache_fresh();
        if(which==0u)card.fail_write=1u;
        if(which==1u)card.fail_sync=1u;
        if(which==2u)card.fail_sync=2u;
        if(which==3u)card.nonzero_final_marker=1u;
        if(which==4u)card.fail_final_read=1u;
        if(which==5u)card.fail_phase_identity=KUI_VMU_PERSIST_STAGE_ZERO_MARKER;
        if(which==6u)card.actual.cid[0]^=1u;
        assert(cached_prepare());stage_handoff_zero();assert(card.work.session.faulted);
    }
}
int main(void) {
    for(unsigned imported=0u;imported<2u;imported++)for(unsigned recovery=0u;recovery<2u;recovery++)equivalent(imported,recovery);
    cache_boundaries();fragmented_snapshot();full_mirrored_fat();admission_memo();live_boot_and_phase_epochs();verified_window_memo();failures();
    puts("PASS VC2 stage batching, exact current handoffs, redo recovery, stale invalidation and failures");return 0;
}
