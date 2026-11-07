/* SPDX-License-Identifier: GPL-3.0-only */
/* Authored sparse FAT/exFAT and image/journal fixture; no original assets. */
#define KUI_VMU_PERSIST_STAGE_MAIN attach_unused_stage_fixture_main
#include "test_vmu_persist_stage.c"
#include "kui/vmu_fs_witness.h"

static struct {
    struct kui_vmu_fs_witness_capture capture;
    uint8_t header[512],manifest_sha[32];
    struct kui_vmu_persist_attach_source source;
    _Alignas(32) uint8_t scratch[KUI_VMU_FS_WITNESS_SCRATCH_BYTES];
    unsigned runs,sectors,fail_run,wrong_identity_at,writes_before_admission,mutate_profile_at;
} attached;

static int attach_capture_read(void *context,uint32_t sector,uint8_t out[512]) {
    return stage_read(context,sector,out);
}
static int attach_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    int result=stage_identity(context,out);
    if(attached.wrong_identity_at && card.identities>=attached.wrong_identity_at)out->cid[0]^=1u;
    if(attached.mutate_profile_at && card.identities==attached.mutate_profile_at)
        card.expected.geometry.volume_serial^=1u;
    return result;
}
static int attach_run(void *context,uint32_t first,uint32_t count,uint8_t *out) {
    assert(context==&card && count && count<=KUI_VMU_FS_WITNESS_RUN_BLOCKS);
    assert(card.work.session.self==&card.work.session && card.work.session.started &&
        card.work.session.busy && card.work.session.callback_active && !card.work.session.admitted &&
        card.work.session.phase==KUI_VMU_PERSIST_SESSION_WITNESS_CHECK);
    ++attached.runs;
    if(attached.fail_run==attached.runs)return -1;
    for(uint32_t i=0u;i<count;i++) {
        if(stage_read(context,(uint64_t)first+i,out+i*512u))return -1;
        ++attached.sectors;
    }
    return 0;
}
static int attach_write(void *context,uint64_t sector,const void *in) {
    if(!card.work.session.admitted || card.work.session.phase==KUI_VMU_PERSIST_SESSION_WITNESS_CHECK)
        ++attached.writes_before_admission;
    assert(!attached.writes_before_admission && card.work.session.fs_result==0);
    return stage_write(context,sector,in);
}
static struct kui_vmu_persist_attach_ops attach_ops(void) {
    return (struct kui_vmu_persist_attach_ops){
        {&card,attach_identity,stage_read,attach_write,stage_sync,stage_health},attach_run};
}
static void attach_capture(void) {
    memset(&attached,0,sizeof(attached));
    struct kui_vmu_fs_witness_binding binding={.card_sectors=card.expected.card_sectors,
        .partition_first=card.expected.partition_first,.partition_end=card.expected.partition_end,
        .high_capacity=card.expected.high_capacity,.generation=1u};
    memcpy(binding.cid,card.expected.cid,16u);memcpy(binding.image_id,card.expected.image_id,16u);
    assert(!kui_vmu_persist_profile_ownership_sha256(&card.expected,attached.source.ownership_sha256));
    struct kui_sha256 hash;kui_sha256_init(&hash);
    static const char collection[]="authored collection fixture V1";
    kui_sha256_update(&hash,collection,sizeof(collection));
    kui_sha256_update(&hash,attached.source.ownership_sha256,32u);kui_sha256_digest(&hash,binding.binding_sha256);
    assert(!kui_vmu_fs_witness_capture_begin(&attached.capture,&binding,attach_capture_read,&card));
    struct kui_vmu_fs_input input=disk.input;
    input.read_sector=kui_vmu_fs_witness_capture_read;input.context=&attached.capture;
    int result=kui_vmu_fs_admit(&input,&card.expected,&disk.work);
    assert(!result);
    assert(!kui_vmu_fs_witness_capture_finish(&attached.capture,result,attached.header,attached.manifest_sha));
    attached.source.header=attached.header;attached.source.records=attached.capture.records;
    attached.source.records_bytes=(size_t)attached.capture.count*KUI_VMU_FS_WITNESS_RECORD_BYTES;
    attached.source.expected_binding=binding;
    memcpy(attached.source.expected_manifest_sha256,attached.manifest_sha,32u);
    card.reads=card.identities=card.writes=card.syncs=0u;
}
static void attach_fresh(void) {stage_fresh();attach_capture();}
static int attach_session(void) {
    struct kui_vmu_persist_attach_ops ops=attach_ops();card.work.phase=KUI_VMU_PERSIST_STAGE_RECOVER;
    return kui_vmu_persist_session_attach(&card.work.session,&card.expected,&attached.source,&ops,
        attached.scratch,sizeof(attached.scratch),&card.work.image);
}
static int attach_stage(void) {
    struct kui_vmu_persist_attach_ops ops=attach_ops();
    return kui_vmu_persist_stage_attach(&card.expected,&card.game,&attached.source,&ops,&card.work,&card.handoff);
}
static void attach_snapshot_policy(void) {
    for(unsigned recovery=0u;recovery<2u;recovery++) {
        struct kui_vmu_durable_handoff expected;
        attach_fresh();if(recovery)stage_seed_real_marker();else card.base[123][7]^=0x71u;
        assert(!attach_stage());expected=card.handoff;
        unsigned reads=card.reads,writes=card.writes,syncs=card.syncs;
        attach_fresh();if(recovery)stage_seed_real_marker();else card.base[123][7]^=0x71u;
        struct kui_vmu_persist_attach_ops ops=attach_ops();
        assert(!kui_vmu_persist_stage_attach_policy(&card.expected,&card.game,&attached.source,&ops,
            &kui_vmu_persist_stage_exclusive_checkpoint,&card.work,&card.handoff));
        assert(!memcmp(&expected,&card.handoff,sizeof(expected)) && card.reads+526u==reads);
        assert(card.writes==writes && card.syncs==syncs && !attached.writes_before_admission);
    }
    /* The extended entry retains the complete witness and identity fences. */
    attach_fresh();attached.header[120]^=1u;
    struct kui_vmu_persist_attach_ops ops=attach_ops();
    assert(kui_vmu_persist_stage_attach_policy(&card.expected,&card.game,&attached.source,&ops,
        &kui_vmu_persist_stage_exclusive_checkpoint,&card.work,&card.handoff));
    stage_handoff_zero();assert(!card.writes && !card.syncs);
}
static void attach_current_image_and_real_recovery(void) {
    attach_fresh();card.base[123][7]=0x71u;
    assert(!attach_session());assert(card.work.session.ready && card.work.session.snapshot_valid);
    assert(!card.writes && !card.syncs && attached.sectors==attached.capture.count);
    assert(card.work.session.block_crc[123]==kui_retail_crc32(0,card.base[123],512u));
    assert(memcmp(card.work.session.base_sha256,card.expected.base_sha256,32u));
    uint8_t next[512];memset(next,0x5c,sizeof(next));
    assert(!kui_vmu_persist_session_commit(&card.work.session,123u,next));
    assert(!memcmp(card.base[123],next,512u));
    struct kui_vmu_persist_session moved=card.work.session;uint8_t out[512];
    assert(kui_vmu_persist_session_read(&moved,123u,out)==KUI_VMU_PERSIST_SESSION_ARGUMENT);
    assert(attach_session()==KUI_VMU_PERSIST_SESSION_BLOCKED);
    attach_fresh();stage_seed_real_marker();
    assert(!attach_stage());assert(card.writes==2u && card.syncs==4u);
    assert(!kui_vmu_durable_handoff_validate(&card.handoff) && sizeof(card.handoff)==1272u);
    for(unsigned i=0u;i<512u;i++)assert(card.base[123][i]==0x79u && !card.journal[1][i]);
    assert(card.handoff.block_crc[123]==kui_retail_crc32(0,card.base[123],512u));
    assert(attached.sectors==attached.capture.count && attached.runs<=attached.sectors &&
        !attached.writes_before_admission);
}
static void attach_refuse_before_any_recovery_write(void) {
    for(unsigned which=0u;which<10u;which++) {
        attach_fresh();stage_seed_real_marker();
        uint8_t base[512],marker[512];memcpy(base,card.base[123],512u);memcpy(marker,card.journal[1],512u);
        if(which==0u)disk.dirs[3][1]^=1u;
        if(which==1u)attached.header[120]^=1u;
        if(which==2u)attached.source.expected_manifest_sha256[0]^=1u;
        if(which==3u)++attached.source.expected_binding.generation;
        if(which==4u)attached.source.expected_binding.binding_sha256[0]^=1u;
        if(which==5u)attached.source.ownership_sha256[0]^=1u;
        if(which==6u)card.actual.cid[0]^=1u;
        if(which==7u)attached.wrong_identity_at=2u;
        if(which==8u)attached.fail_run=1u;
        if(which==9u)attached.capture.records[0].sha256[0]^=1u;
        assert(attach_stage());stage_handoff_zero();
        assert(!card.writes && !card.syncs && !memcmp(base,card.base[123],512u) &&
            !memcmp(marker,card.journal[1],512u));
    }
    attach_fresh();stage_seed_real_marker();
    card.game.slots[1].extent.card_lba=lba(60u)+2u; /* journal's allocated slack */
    assert(attach_stage()==KUI_VMU_PERSIST_STAGE_MAPPING && !card.reads && !card.writes);
    stage_handoff_zero();
    attach_fresh();card.base[255][0]=0u;
    assert(attach_stage() && !card.writes);stage_handoff_zero();
}
static void attach_partial_journal_and_power_cuts(void) {
    for(unsigned which=0u;which<2u;which++) {
        attach_fresh();stage_seed_real_marker();
        if(which) {
            card.journal[0][123]^=1u;
            assert(attach_stage() && !card.writes);stage_handoff_zero();
        } else {
            /* A torn marker is uncommitted. Preserve the current base and
             * only clear that marker at the explicit final checkpoint. */
            card.journal[1][123]^=1u;
            assert(!attach_stage() && card.writes==1u);
            for(unsigned i=0u;i<512u;i++)assert(!card.base[123][i]);
        }
    }
    for(unsigned kind=0u;kind<2u;kind++)for(unsigned cut=1u;cut<=4u;cut++) {
        attach_fresh();assert(!attach_session());
        uint8_t before[512],next[512];memcpy(before,card.base[123],512u);memset(next,0xa7,sizeof(next));
        if(kind)card.fail_sync=cut;else card.fail_write=cut;
        assert(kui_vmu_persist_session_commit(&card.work.session,123u,next));
        assert(card.work.session.faulted);
        /* The old owner is abandoned. A different zeroed session gets fresh
         * witness replay + real-map recovery, never copies the old session. */
        memset(&card.work,0,sizeof(card.work));card.fail_sync=card.fail_write=0u;
        card.reads=card.writes=card.syncs=card.identities=0u;attached.runs=attached.sectors=0u;
        assert(!attach_stage());
        assert(!memcmp(card.base[123],before,512u) || !memcmp(card.base[123],next,512u));
        assert(!kui_vmu_durable_handoff_validate(&card.handoff));
        for(unsigned i=0u;i<512u;i++)assert(!card.journal[1][i]);
    }
}
static void attach_identity_fences_and_argument_guards(void) {
    attach_fresh();attached.wrong_identity_at=1u;
    assert(attach_session()==KUI_VMU_PERSIST_SESSION_IDENTITY && !attached.runs && !card.writes);
    attach_fresh();stage_seed_real_marker();attached.wrong_identity_at=3u;
    assert(attach_session()==KUI_VMU_PERSIST_SESSION_IDENTITY && !card.writes);
    attach_fresh();attached.mutate_profile_at=3u;
    assert(attach_stage() && !card.writes);stage_handoff_zero();
    attach_fresh();struct kui_vmu_persist_attach_ops ops=attach_ops();
    assert(kui_vmu_persist_session_attach(&card.work.session,&card.expected,&attached.source,&ops,
        attached.scratch+1u,sizeof(attached.scratch)-1u,&card.work.image)==KUI_VMU_PERSIST_SESSION_ARGUMENT);
    assert(!card.work.session.started && !card.reads && !card.writes);
    struct {struct kui_vmu_persist_attach_source source;uint8_t tail[512];} forged;
    forged.source=attached.source;forged.source.header=(const uint8_t *)&forged.source;
    assert(kui_vmu_persist_session_attach(&card.work.session,&card.expected,&forged.source,&ops,
        attached.scratch,sizeof(attached.scratch),&card.work.image)==KUI_VMU_PERSIST_SESSION_ARGUMENT);
    assert(!card.work.session.started && !card.reads && !card.writes);
    assert(kui_vmu_persist_session_attach(&card.work.session,&card.expected,&attached.source,&ops,
        &card.work.session,sizeof(card.work.session),&card.work.image)==KUI_VMU_PERSIST_SESSION_ARGUMENT);
    ops.read_run=NULL;
    assert(kui_vmu_persist_session_attach(&card.work.session,&card.expected,&attached.source,&ops,
        attached.scratch,sizeof(attached.scratch),&card.work.image)==KUI_VMU_PERSIST_SESSION_ARGUMENT);
    assert(!card.work.session.started && !card.reads && !card.writes);
}
static void ownership_hash_canonical_equivalence(void) {
    attach_fresh();uint8_t first[32],next[32];
    assert(!kui_vmu_persist_profile_ownership_sha256(&card.expected,first));
    struct kui_vmu_persist_profile changed=card.expected;
    memset(changed.base_sha256,0xa3,32u);memset(changed.journal_sha256,0x97,32u);
    memset(changed.identity_sha256,0x81,32u);memset(changed.marker_sha256,0x7b,32u);
    memset(changed.block_crc,0xff,sizeof(changed.block_crc));changed.journal_sequence=UINT64_MAX;
    assert(kui_vmu_persist_profile_same_ownership(&card.expected,&changed));
    assert(!kui_vmu_persist_profile_ownership_sha256(&changed,next) && !memcmp(first,next,32u));
    for(size_t i=0u;i<sizeof(changed);i++) {
        changed=card.expected;((uint8_t *)&changed)[i]^=1u;
        int same=kui_vmu_persist_profile_same_ownership(&card.expected,&changed);
        int result=kui_vmu_persist_profile_ownership_sha256(&changed,next);
        if(same)assert(!result && !memcmp(first,next,32u));
        else assert(result || memcmp(first,next,32u));
    }
    changed=card.expected;
    assert(kui_vmu_persist_profile_ownership_sha256(&changed,(uint8_t *)&changed));
    assert(!memcmp(&changed,&card.expected,sizeof(changed)));
}
int main(int argc,char **argv) {
    if(argc==2) {
        stage_fresh();uint8_t wire[4096],digest[32];
        assert(!kui_vmu_persist_profile_encode(&card.expected,wire));
        assert(!kui_vmu_persist_profile_ownership_sha256(&card.expected,digest));
        FILE *file=fopen(argv[1],"wb");assert(file);
        assert(fwrite(wire,1u,sizeof(wire),file)==sizeof(wire));
        assert(fwrite(digest,1u,sizeof(digest),file)==sizeof(digest));assert(!fclose(file));
        return 0;
    }
    assert(argc==1);
    ownership_hash_canonical_equivalence();attach_current_image_and_real_recovery();
    attach_refuse_before_any_recovery_write();attach_partial_journal_and_power_cuts();
    attach_identity_fences_and_argument_guards();attach_snapshot_policy();
    puts("PASS collection attach: complete fresh witness/profile/CID fences, no writes before verified admission, current image/real journal recovery, power-cut reopen,1272-byte handoff, whole-cluster protection and canonical ownership SHA");
    return 0;
}
