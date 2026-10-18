/* SPDX-License-Identifier: GPL-3.0-only */
#define main vmu_persist_stage_unused_fs_fixture_main
#include "test_vmu_fs_admission.c"
#undef main
#include "kui/vmu_persist_stage.h"
#include "kui/retail_image.h"

static struct {
    uint8_t base[256][512],journal[2][512];
    struct kui_vmu_persist_profile expected;
    struct kui_retail_manifest game;
    struct kui_vmu_persist_stage_workspace work;
    struct kui_vmu_durable_handoff handoff;
    struct kui_vmu_persist_session_identity actual;
    unsigned reads,writes,syncs,identities,fail_write,fail_sync,fail_phase_identity;
    unsigned fail_final_read,nonzero_final_marker,damage_after_checkpoint;
} card;
static uint8_t *stage_sector(uint64_t sector) {
    uint64_t start=lba(20u);
    if(sector>=start && sector-start<256u) return card.base[sector-start];
    start=lba(60u);
    if(sector>=start && sector-start<2u) return card.journal[sector-start];
    return NULL;
}
static bool stage_health(void *context) {assert(context==&card);return true;}
static int stage_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    assert(context==&card);++card.identities;*out=card.actual;
    if(card.fail_phase_identity==card.work.phase) out->cid[0]^=1u;
    return 0;
}
static int stage_read(void *context,uint64_t sector,void *out) {
    assert(context==&card);++card.reads;
    if(card.work.phase==KUI_VMU_PERSIST_STAGE_ZERO_MARKER && sector==lba(60u)+1u) {
        if(card.fail_final_read) return -1;
        if(card.nonzero_final_marker) {memset(out,0,512u);((uint8_t *)out)[511]=1u;return 0;}
    }
    uint8_t *p=stage_sector(sector);
    if(p) memcpy(out,p,512u);
    else {assert(sector<=UINT32_MAX);return read_sector(&disk,(uint32_t)sector,out);}
    return 0;
}
static int stage_write(void *context,uint64_t sector,const void *in) {
    assert(context==&card);uint8_t *p=stage_sector(sector);assert(p);++card.writes;
    if(card.fail_write==card.writes) return -1;
    memcpy(p,in,512u);return 0;
}
static int stage_sync(void *context) {
    assert(context==&card);++card.syncs;
    if(card.fail_sync==card.syncs) return -1;
    if(card.damage_after_checkpoint && card.writes) card.base[255][0]=0u;
    return 0;
}
static struct kui_vmu_persist_session_ops stage_ops(void) {
    return (struct kui_vmu_persist_session_ops){&card,stage_identity,stage_read,stage_write,stage_sync,stage_health};
}
static int stage_role_read(void *context,unsigned role,uint32_t logical,uint8_t out[512]) {
    uint32_t sector;assert(!kui_vmu_persist_profile_map_lba(&card.expected,role,logical,&sector));
    return stage_read(context,sector,out);
}
static void stage_fresh(void) {
    fresh();memset(&card,0,sizeof(card));
    for(unsigned b=0;b<256u;b++) assert(!kui_vmu_image_blank_block((uint16_t)b,card.base[b]));
    assert(!kui_vmu_fs_collect(&disk.input,&card.expected,&disk.work));
    memcpy(card.actual.cid,disk.input.cid,16u);card.actual.card_sectors=disk.input.card_sectors;
    card.actual.high_capacity=disk.input.high_capacity;
    assert(!kui_vmu_persist_profile_collect_snapshot(&card.expected,stage_role_read,&card));
    card.reads=card.identities=0u;
    struct kui_retail_manifest *m=&card.game;
    m->card_sectors=FS_SECTORS;m->partition_end=FS_SECTORS;m->storage_transport=KUI_STORAGE_SCI;
    m->reader=KUI_RETAIL_READER_STANDARD;m->track_count=1u;m->extent_count=1u;
    m->session_lba=45000u;m->boot_lba=45000u;m->boot_bytes=2048u;
    memcpy(m->title,"Persistent stage fixture",25u);memcpy(m->bootfile,"1ST_READ.BIN",13u);
    m->slots[0].track=(struct kui_retail_track){45000u,45004u,1u,1u,4u};
    m->slots[1].extent=(struct kui_retail_extent){0u,1000u,19u};
    assert(kui_retail_manifest_validate(m)==KUI_GAME_OK);
}
static int stage_prepare(void) {
    struct kui_vmu_persist_session_ops ops=stage_ops();
    return kui_vmu_persist_stage_prepare(&card.expected,&card.game,&ops,&card.work,&card.handoff);
}
static void stage_handoff_zero(void) {
    const uint8_t *p=(const uint8_t *)&card.handoff;
    for(size_t i=0;i<sizeof(card.handoff);i++) assert(!p[i]);
}
static void stage_clean_handoff_and_current_content(void) {
    stage_fresh();card.base[123][7]=0x71u; /* A legitimate saved free-sector byte. */
    assert(!stage_prepare());assert(card.work.phase==KUI_VMU_PERSIST_STAGE_SEAL);
    assert(card.writes==1u && card.syncs==2u);
    assert(!kui_vmu_durable_handoff_validate(&card.handoff));
    assert(!kui_vmu_persist_stage_handoff_matches(&card.expected,&card.handoff));
    assert(memcmp(card.handoff.base_sha256,card.expected.base_sha256,32u));
    assert(card.handoff.block_crc[123]==kui_retail_crc32(0,card.base[123],512u));
    assert(card.handoff.base[0].lba==lba(20u) && card.handoff.base[0].blocks==256u);
    assert(card.handoff.journal[0].lba==lba(60u) && card.handoff.journal[0].blocks==2u);
    for(unsigned i=0;i<512u;i++) assert(!card.journal[1][i]);
    card.handoff.block_crc[0]^=1u;assert(kui_vmu_persist_stage_handoff_matches(&card.expected,&card.handoff));
    assert(!kui_vmu_durable_handoff_seal(&card.handoff));
    assert(!kui_vmu_persist_stage_handoff_matches(&card.expected,&card.handoff)); /* Current hashes need not equal prep. */
    card.handoff.cid[0]^=1u;assert(!kui_vmu_durable_handoff_seal(&card.handoff));
    assert(kui_vmu_persist_stage_handoff_matches(&card.expected,&card.handoff));
}
static void stage_all_whole_allocations_and_audio_protected(void) {
    for(unsigned role=0;role<3u;role++) {
        stage_fresh();
        card.game.slots[1].extent.card_lba=role==0u?lba(20u)+200u:role==1u?lba(60u)+2u:lba(61u)+1u;
        assert(kui_retail_manifest_validate(&card.game)==KUI_GAME_OK);
        assert(kui_vmu_persist_stage_admit(&card.expected,&card.game)==KUI_VMU_PERSIST_STAGE_MAPPING);
        assert(stage_prepare()==KUI_VMU_PERSIST_STAGE_MAPPING);
        assert(!card.reads && !card.writes && !card.syncs);stage_handoff_zero();
    }
    stage_fresh();struct kui_retail_manifest *m=&card.game;
    m->track_count=2u;m->extent_count=2u;m->slots[0].track.first_extent=2u;
    m->slots[1].track=(struct kui_retail_track){45004u,45005u,3u,1u,0u};
    m->slots[2].extent=(struct kui_retail_extent){0u,1000u,19u};
    m->slots[3].extent=(struct kui_retail_extent){0u,lba(60u)+2u,5u};
    assert(kui_retail_manifest_validate(m)==KUI_GAME_OK);
    assert(stage_prepare()==KUI_VMU_PERSIST_STAGE_MAPPING && !card.reads && !card.writes);
    stage_handoff_zero();
    stage_fresh();card.game.card_sectors=card.game.partition_end;
    assert(!kui_vmu_persist_stage_admit(&card.expected,&card.game));
    card.game.storage_transport=KUI_STORAGE_SCIF;assert(kui_vmu_persist_stage_admit(&card.expected,&card.game));
    stage_fresh();card.game.reader=KUI_RETAIL_READER_ASYNC;assert(kui_vmu_persist_stage_admit(&card.expected,&card.game));
    stage_fresh();++card.game.partition_start;assert(kui_vmu_persist_stage_admit(&card.expected,&card.game));
}
static void stage_seed_real_marker(void) {
    struct kui_vmu_store store;struct kui_vmu_store_extent base={lba(20u),256u},journal={lba(60u),2u};
    struct kui_vmu_store_config config={.card_sectors=FS_SECTORS,.image_extents=&base,.image_extent_count=1u,
        .journal_extents=&journal,.journal_extent_count=1u};memcpy(config.image_id,card.expected.image_id,16u);
    struct kui_vmu_store_io io={&card,stage_read,stage_write,stage_sync};
    assert(!kui_vmu_store_open(&store,&io,&config));uint8_t next[512];memset(next,0x79,sizeof(next));
    assert(!kui_vmu_store_write(&store,123u,next));memset(card.base[123],0,512u);
    card.writes=card.syncs=card.reads=card.identities=0u;
}
static void stage_recovery_and_fresh_admission_first(void) {
    stage_fresh();stage_seed_real_marker();assert(!stage_prepare());
    assert(card.writes==2u && card.base[123][0]==0x79u);
    for(unsigned i=0;i<512u;i++) assert(!card.journal[1][i]);
    assert(card.handoff.block_crc[123]==kui_retail_crc32(0,card.base[123],512u));
    stage_fresh();stage_seed_real_marker();file_entry(disk.dirs[3]+96u,"card.journal",32u,62u,1024u,3u);allocated(62u,1);
    assert(stage_prepare()==KUI_VMU_PERSIST_STAGE_SESSION && !card.writes && !card.syncs);
    assert(card.work.session.fs_result==KUI_VMU_FS_CHANGED);stage_handoff_zero();
    /* The current directory can move a target into a protected game range
     * even when its old embedded map was disjoint. Fresh admission rechecks. */
    stage_fresh();stage_seed_real_marker();assert(lba(122u)==1000u);
    file_entry(disk.dirs[3]+96u,"card.journal",32u,122u,1024u,3u);allocated(122u,1);
    assert(!kui_vmu_persist_stage_admit(&card.expected,&card.game));
    assert(stage_prepare()==KUI_VMU_PERSIST_STAGE_SESSION && !card.writes && !card.syncs);
    assert(card.work.session.fs_result==KUI_VMU_FS_PROTECTED);stage_handoff_zero();
    stage_fresh();stage_seed_real_marker();card.journal[1][40]^=1u;
    p32(card.journal[1]+508u,kui_retail_crc32(0,card.journal[1],508u));
    assert(stage_prepare()==KUI_VMU_PERSIST_STAGE_SESSION && !card.writes && !card.syncs);
    assert(card.work.session.store_result==KUI_VMU_STORE_IDENTITY);stage_handoff_zero();
}
static void stage_checkpoint_snapshot_and_final_faults(void) {
    for(unsigned which=0;which<8u;which++) {
        stage_fresh();memset(&card.handoff,0xa5,sizeof(card.handoff));
        if(which==0u) card.fail_write=1u;
        if(which==1u) card.fail_sync=1u;
        if(which==2u) card.fail_sync=2u;
        if(which==3u) card.damage_after_checkpoint=1u;
        if(which==4u) card.nonzero_final_marker=1u;
        if(which==5u) card.fail_final_read=1u;
        if(which==6u) card.fail_phase_identity=KUI_VMU_PERSIST_STAGE_ZERO_MARKER;
        if(which==7u) card.actual.cid[0]^=1u;
        assert(stage_prepare());stage_handoff_zero();assert(card.work.session.faulted);
        if(which==7u) assert(!card.reads && !card.writes && !card.syncs);
    }
}
static void stage_arguments_and_no_workspace_reuse(void) {
    stage_fresh();struct kui_vmu_persist_session_ops ops=stage_ops();ops.healthy=NULL;
    memset(&card.handoff,0xa5,sizeof(card.handoff));
    assert(kui_vmu_persist_stage_prepare(&card.expected,&card.game,&ops,&card.work,&card.handoff)==KUI_VMU_PERSIST_STAGE_ARGUMENT);
    stage_handoff_zero();assert(!card.reads && !card.writes && !card.work.started);
    assert(kui_vmu_persist_stage_handoff_matches(&card.expected,NULL));
    assert(kui_vmu_persist_stage_handoff_matches(NULL,&card.handoff));
    assert(!stage_prepare());unsigned calls=card.reads+card.writes+card.syncs+card.identities;
    assert(stage_prepare()==KUI_VMU_PERSIST_STAGE_ARGUMENT);stage_handoff_zero();
    assert(card.reads+card.writes+card.syncs+card.identities==calls);
}
int main(void) {
    stage_clean_handoff_and_current_content();stage_all_whole_allocations_and_audio_protected();
    stage_recovery_and_fresh_admission_first();stage_checkpoint_snapshot_and_final_faults();
    stage_arguments_and_no_workspace_reuse();
    puts("VMU durable stage: whole allocation/audio exclusions, fresh physical recovery, checkpoint/current hashes and sealed handoff failure guards passed");
    return 0;
}
