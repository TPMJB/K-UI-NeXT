/* SPDX-License-Identifier: GPL-3.0-only */
/* The real shared stage runs with conservative and explicitly reviewed
 * backend contracts. Compare complete sealed output and actual callbacks. */
#define KUI_VMU_PERSIST_STAGE_MAIN snapshot_policy_reused_fixture_main
#include "test_vmu_persist_stage.c"
#ifndef KUI_TEST_STAGE_SNAPSHOT_POLICY
#define KUI_TEST_STAGE_SNAPSHOT_POLICY 0
#endif
static int snapshot_prepare(void) {
    struct kui_vmu_persist_session_ops ops=stage_ops();
#if KUI_TEST_STAGE_SNAPSHOT_POLICY
    return kui_vmu_persist_stage_prepare_policy(&card.expected,&card.game,&ops,
        &kui_vmu_persist_stage_exclusive_checkpoint,
        &card.work,&card.handoff);
#else
    return kui_vmu_persist_stage_prepare(&card.expected,&card.game,&ops,&card.work,&card.handoff);
#endif
}

static void snapshot_check(FILE *out,unsigned recovery,unsigned imported) {
    if(imported)stage_imported_pair_fresh();else stage_fresh();
    if(recovery)stage_seed_real_marker();
    else card.base[123][7]^=0x71u;
    uint8_t old_before[32],old_after[32];
    if(imported)stage_old_pair_digest(old_before);
    assert(!snapshot_prepare());
    assert(card.work.phase==KUI_VMU_PERSIST_STAGE_SEAL && card.work.session.snapshot_valid);
    assert(card.writes==(recovery?2u:1u) && card.syncs==(recovery?4u:2u));
    assert(card.reads-card.work.filesystem.stats.reads==
        (KUI_TEST_STAGE_SNAPSHOT_POLICY?529u:1055u)+recovery);
    assert(card.identities==(KUI_TEST_STAGE_SNAPSHOT_POLICY?11u:13u)+(recovery?3u:0u));
    assert(!kui_vmu_durable_handoff_validate(&card.handoff));
    assert(!kui_vmu_persist_stage_handoff_matches(&card.expected,&card.handoff));
    struct kui_sha256 hash;uint8_t sha[32];kui_sha256_init(&hash);
    kui_sha256_update(&hash,card.base,sizeof(card.base));kui_sha256_digest(&hash,sha);
    assert(!memcmp(sha,card.handoff.base_sha256,sizeof(sha)));
    for(unsigned block=0u;block<256u;block++)
        assert(card.handoff.block_crc[block]==kui_retail_crc32(0u,card.base[block],512u));
    for(unsigned i=0u;i<512u;i++)assert(!card.journal[1][i]);
    if(recovery)for(unsigned i=0u;i<512u;i++)assert(card.base[123][i]==0x79u);
    if(imported) {
        assert(card.work.session.summary.files==13u && card.work.session.summary.used_blocks==196u);
        stage_old_pair_digest(old_after);assert(!memcmp(old_before,old_after,sizeof(old_before)));
    }
    assert(fwrite(&card.handoff,1u,sizeof(card.handoff),out)==sizeof(card.handoff));
    printf("snapshot_policy=%u imported=%u recovery=%u fs_reads=%u other_reads=%u writes=%u syncs=%u identities=%u\n",
        KUI_TEST_STAGE_SNAPSHOT_POLICY,imported,recovery,card.work.filesystem.stats.reads,
        card.reads-card.work.filesystem.stats.reads,card.writes,card.syncs,card.identities);
}
static void snapshot_failures(void) {
    stage_fresh();card.base[255][0]=0u;
    assert(snapshot_prepare()==KUI_VMU_PERSIST_STAGE_SESSION && !card.writes && !card.syncs);
    stage_handoff_zero();
    stage_recovery_and_fresh_admission_first();
    for(unsigned which=0u;which<7u;which++) {
        stage_fresh();
        if(which==0u)card.fail_write=1u;
        if(which==1u)card.fail_sync=1u;
        if(which==2u)card.fail_sync=2u;
        if(which==3u)card.nonzero_final_marker=1u;
        if(which==4u)card.fail_final_read=1u;
        if(which==5u)card.fail_phase_identity=KUI_VMU_PERSIST_STAGE_ZERO_MARKER;
        if(which==6u)card.actual.cid[0]^=1u;
        assert(snapshot_prepare());stage_handoff_zero();assert(card.work.session.faulted);
    }
}
static unsigned invalidate_snapshot,fail_final_health;
static struct kui_vmu_persist_stage_policy *late_policy;
static int policy_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    if(late_policy)late_policy->flags=KUI_VMU_PERSIST_STAGE_CHECKPOINT_SNAPSHOT;
    if(invalidate_snapshot && card.work.phase==KUI_VMU_PERSIST_STAGE_CHECKPOINT)
        card.work.session.snapshot_valid=0u;
    return stage_identity(context,out);
}
static bool policy_health(void *context) {
    return stage_health(context) && !(fail_final_health && card.work.phase==KUI_VMU_PERSIST_STAGE_ZERO_MARKER);
}
static int policy_prepare(const struct kui_vmu_persist_stage_policy *policy,bool disc) {
    struct kui_vmu_persist_session_ops ops=stage_ops();
    ops.identity=policy_identity;ops.healthy=policy_health;
    return disc?kui_vmu_persist_disc_stage_prepare_policy(&card.expected,KUI_STORAGE_SCI,
        &ops,policy,&card.work,&card.handoff):
        kui_vmu_persist_stage_prepare_policy(&card.expected,&card.game,&ops,policy,
        &card.work,&card.handoff);
}
static void policy_guards(void) {
    for(unsigned disc=0u;disc<2u;disc++) {
        for(unsigned bad=0u;bad<4u;bad++) {
            stage_fresh();struct kui_vmu_persist_stage_policy policy=kui_vmu_persist_stage_exclusive_checkpoint;
            if(bad==0u)++policy.version;
            if(bad==1u)--policy.bytes;
            if(bad==2u)policy.flags|=2u;
            if(bad==3u)policy.reserved=1u;
            memset(&card.handoff,0xa5,sizeof(card.handoff));
            assert(policy_prepare(&policy,disc)==KUI_VMU_PERSIST_STAGE_ARGUMENT);
            stage_handoff_zero();assert(!card.reads && !card.writes && !card.syncs && !card.work.started);
        }
        /* Unknown/mutating backends keep the historical independent reread:
         * never apply a capability merely because the transport is SCI. */
        stage_fresh();card.damage_after_checkpoint=1u;
        assert(policy_prepare(NULL,disc));stage_handoff_zero();
        /* A lost snapshot cannot be replaced by preparation-record hashes. */
        stage_fresh();invalidate_snapshot=1u;card.damage_after_checkpoint=1u;
        assert(policy_prepare(&kui_vmu_persist_stage_exclusive_checkpoint,disc));
        stage_handoff_zero();invalidate_snapshot=0u;
        stage_fresh();invalidate_snapshot=1u;
        assert(!policy_prepare(&kui_vmu_persist_stage_exclusive_checkpoint,disc));
        assert(card.reads-card.work.filesystem.stats.reads==1055u);
        invalidate_snapshot=0u;
        stage_fresh();fail_final_health=1u;
        assert(policy_prepare(&kui_vmu_persist_stage_exclusive_checkpoint,disc));
        stage_handoff_zero();assert(card.work.session.faulted);fail_final_health=0u;
        /* Copy the caller's policy before any I/O; a callback cannot grant
         * a capability by changing that object during the operation. */
        stage_fresh();struct kui_vmu_persist_stage_policy policy=kui_vmu_persist_stage_exclusive_checkpoint;
        policy.flags=0u;late_policy=&policy;card.damage_after_checkpoint=1u;
        assert(policy_prepare(&policy,disc));stage_handoff_zero();late_policy=NULL;
    }
}
static void disc_snapshot_equivalence(void) {
    for(uint32_t transport=KUI_STORAGE_SCIF;transport<=KUI_STORAGE_SCI;transport++)
        for(unsigned imported=0u;imported<2u;imported++)for(unsigned recovery=0u;recovery<2u;recovery++) {
            struct kui_vmu_durable_handoff expected;unsigned old_reads=0u;
            for(unsigned reuse=0u;reuse<2u;reuse++) {
                if(imported)stage_imported_pair_fresh();else stage_fresh();
                if(recovery)stage_seed_real_marker();else card.base[123][7]^=0x71u;
                struct kui_vmu_persist_session_ops ops=stage_ops();
                assert(!kui_vmu_persist_disc_stage_prepare_policy(&card.expected,transport,&ops,
                    reuse?&kui_vmu_persist_stage_exclusive_checkpoint:NULL,&card.work,&card.handoff));
                if(!reuse) {expected=card.handoff;old_reads=card.reads;}
                else assert(!memcmp(&expected,&card.handoff,sizeof(expected)) && card.reads+526u==old_reads);
                assert(card.writes==(recovery?2u:1u) && card.syncs==(recovery?4u:2u));
            }
        }
}
int main(int argc,char **argv) {
    const struct kui_vmu_persist_stage_policy expected_policy={1u,16u,1u,0u};
    assert(!memcmp(&expected_policy,&kui_vmu_persist_stage_exclusive_checkpoint,sizeof(expected_policy)));
    assert(argc==2);FILE *out=fopen(argv[1],"wb");assert(out);
    for(unsigned imported=0u;imported<2u;imported++)
        for(unsigned recovery=0u;recovery<2u;recovery++)snapshot_check(out,recovery,imported);
    assert(!fclose(out));snapshot_failures();policy_guards();disc_snapshot_equivalence();
    puts("PASS real-core stage snapshot equivalence, current CRC/SHA, pending recovery, imported saves, and final fault guards");
    return 0;
}
